/// \file
/// \brief Implementation of [Go to Definition]
/// [Go to Definition]:
/// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_definition

#include "Definition.h"
#include "AST.h"
#include "CheckReturn.h"
#include "Convert.h"
#include "PathResolve.h"

#include "nixd/Controller/Controller.h"
#include "nixd/Protocol/AttrSet.h"

#include "lspserver/Protocol.h"

#include <boost/asio/post.hpp>

#include <llvm/Support/Error.h>
#include <llvm/Support/JSON.h>

#include <nixf/Basic/Nodes/Attrs.h>
#include <nixf/Basic/Nodes/Basic.h>
#include <nixf/Basic/Nodes/Expr.h>
#include <nixf/Basic/Nodes/Lambda.h>
#include <nixf/Basic/Nodes/Simple.h>
#include <nixf/Parse/Parser.h>
#include <nixf/Sema/ParentMap.h>
#include <nixf/Sema/VariableLookup.h>

#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <semaphore>
#include <sstream>

using namespace nixd;
using namespace nixd::idioms;
using namespace nixf;
using namespace lspserver;
using namespace llvm;

using LookupResult = VariableLookupAnalysis::LookupResult;
using ResultKind = VariableLookupAnalysis::LookupResultKind;
using Locations = std::vector<Location>;

namespace {

const Definition *findSelfDefinition(const Node &N,
                                     const ParentMapAnalysis &PMA,
                                     const VariableLookupAnalysis &VLA) {
  // If "N" is a definition itself, just return it.
  if (const Definition *Def = VLA.toDef(N))
    return Def;

  // If N is inside an attrset, it maybe an "AttrName", let's look for it.
  const Node *Parent = PMA.query(N);
  if (Parent && Parent->kind() == Node::NK_AttrName)
    return VLA.toDef(*Parent);

  return nullptr;
}

// Special case, variable in "inherit"
// inherit name
//         ^~~~<---  this is an "AttrName", not variable.
const ExprVar *findInheritVar(const Node &N, const ParentMapAnalysis &PMA,
                              const VariableLookupAnalysis &VLA) {
  if (const Node *Up = PMA.upTo(N, Node::NK_Inherit)) {
    const Node *UpAn = PMA.upTo(N, Node::NK_AttrName);
    if (!UpAn)
      return nullptr;
    const auto &Inh = static_cast<const Inherit &>(*Up);
    const auto &AN = static_cast<const AttrName &>(*UpAn);

    // Skip:
    //
    //    inherit (expr) name1 name2;
    //
    if (Inh.expr())
      return nullptr;

    // Skip dynamic.
    if (!AN.isStatic())
      return nullptr;

    // This attrname will be desugared into an "ExprVar".
    Up = PMA.upTo(Inh, Node::NK_ExprAttrs);
    if (!Up)
      return nullptr;

    const SemaAttrs &SA = static_cast<const ExprAttrs &>(*Up).sema();
    const Node *Var = SA.staticAttrs().at(AN.staticName()).value();
    assert(Var->kind() == Node::NK_ExprVar);
    return static_cast<const ExprVar *>(Var);
  }
  return nullptr;
}

const ExprVar *findVar(const Node &N, const ParentMapAnalysis &PMA,
                       const VariableLookupAnalysis &VLA) {
  if (const ExprVar *InVar = findInheritVar(N, PMA, VLA))
    return InVar;

  return static_cast<const ExprVar *>(PMA.upTo(N, Node::NK_ExprVar));
}

const Definition &findVarDefinition(const ExprVar &Var,
                                    const VariableLookupAnalysis &VLA) {
  LookupResult Result = VLA.query(Var);

  if (Result.Kind == ResultKind::Undefined)
    throw UndefinedVarException();

  if (Result.Kind == ResultKind::NoSuchVar)
    throw NoSuchVarException();

  assert(Result.Def);

  return *Result.Def;
}

/// \brief Convert nixf::Definition to lspserver::Location
Location convertToLocation(llvm::StringRef Src, const Definition &Def,
                           URIForFile URI) {
  if (!Def.syntax())
    throw NoLocationForBuiltinVariable();
  assert(Def.syntax());
  return Location{
      .uri = std::move(URI),
      .range = toLSPRange(Src, Def.syntax()->range()),
  };
}

struct NoLocationsFoundInNixpkgsException : std::exception {
  [[nodiscard]] const char *what() const noexcept override {
    return "no locations found in nixpkgs";
  }
};

class WorkerReportedException : std::exception {
  llvm::Error E;

public:
  WorkerReportedException(llvm::Error E) : E(std::move(E)) {};

  llvm::Error takeError() { return std::move(E); }
  [[nodiscard]] const char *what() const noexcept override {
    return "worker reported some error";
  }
};

/// \brief Resolve definition by invoking nixpkgs provider.
///
/// Useful for users inspecting nixpkgs packages. For example, someone clicks
/// "with pkgs; [ hello ]", it's better to goto nixpkgs position, instead of
/// "with pkgs;"
class NixpkgsDefinitionProvider {
  AttrSetClient &NixpkgsClient;

  /// \brief Parse nix-rolled location: file:line -> lsp Location
  static Location parseLocation(std::string_view Position) {
    // Firstly, find ":"
    auto Pos = Position.find_first_of(':');
    if (Pos == std::string_view::npos) {
      return Location{
          .uri = URIForFile::canonicalize(Position, Position),
          .range = {{0, 0}, {0, 0}},
      };
    }
    int PosL = std::stoi(std::string(Position.substr(Pos + 1)));
    lspserver::Position P{PosL, 0};
    std::string_view File = Position.substr(0, Pos);
    return Location{
        .uri = URIForFile::canonicalize(File, File),
        .range = {P, P},
    };
  }

public:
  NixpkgsDefinitionProvider(AttrSetClient &NixpkgsClient)
      : NixpkgsClient(NixpkgsClient) {}

  Locations resolveSelector(const nixd::Selector &Sel) {
    std::binary_semaphore Ready(0);
    Expected<AttrPathInfoResponse> Desc = error("not replied");
    auto OnReply = [&Ready, &Desc](llvm::Expected<AttrPathInfoResponse> Resp) {
      if (Resp)
        Desc = *Resp;
      else
        Desc = Resp.takeError();
      Ready.release();
    };
    NixpkgsClient.attrpathInfo(Sel, std::move(OnReply));
    Ready.acquire();

    if (!Desc)
      throw WorkerReportedException(Desc.takeError());

    // Prioritize package location if it exists.
    if (const std::optional<std::string> &Position = Desc->PackageDesc.Position)
      return Locations{parseLocation(*Position)};

    // Use the location in "ValueMeta".
    if (const auto &Loc = Desc->Meta.Location)
      return Locations{*Loc};

    throw NoLocationsFoundInNixpkgsException();
  }
};

/// \brief Resolve expr path to "real" path, returning a location.
///
/// This enables "go to definition" for path literals like ./foo.nix.
std::optional<Location> definePath(const ExprPath &Path,
                                   const std::string &BasePath) {
  // Only handle literal paths (no interpolation)
  if (!Path.parts().isLiteral())
    return std::nullopt;

  // Use shared path resolution logic
  if (auto Resolved = resolveExprPath(BasePath, Path.parts().literal())) {
    return Location{
        .uri = URIForFile::canonicalize(*Resolved, *Resolved),
        .range = {{0, 0}, {0, 0}},
    };
  }
  return std::nullopt;
}

const Expr *ignoreParens(const Expr *E) {
  while (E && E->kind() == Node::NK_ExprParen)
    E = static_cast<const ExprParen &>(*E).expr();
  return E;
}

const Expr *returnedExpr(const Expr *E) {
  while (const Expr *Unwrapped = ignoreParens(E)) {
    E = Unwrapped;
    switch (E->kind()) {
    case Node::NK_ExprLambda:
      E = static_cast<const ExprLambda &>(*E).body();
      continue;
    case Node::NK_ExprLet:
      E = static_cast<const ExprLet &>(*E).expr();
      continue;
    default:
      return E;
    }
  }
  return nullptr;
}

std::optional<std::string> readFile(const std::string &Path) {
  std::ifstream File(Path);
  if (!File)
    return std::nullopt;

  std::ostringstream Buffer;
  Buffer << File.rdbuf();
  return Buffer.str();
}

struct AnalyzedFile {
  std::string File;
  std::string Src;
  std::vector<nixf::Diagnostic> Diagnostics;
  std::shared_ptr<Node> AST;
  std::unique_ptr<VariableLookupAnalysis> VLA;
  ParentMapAnalysis PM;
};

std::optional<AnalyzedFile> analyzeFile(const std::string &File) {
  std::optional<std::string> Src = readFile(File);
  if (!Src)
    return std::nullopt;

  AnalyzedFile Analyzed{
      .File = File,
      .Src = std::move(*Src),
  };
  Analyzed.AST = parse(Analyzed.Src, Analyzed.Diagnostics);
  if (!Analyzed.AST)
    return std::nullopt;

  Analyzed.PM.runOnAST(*Analyzed.AST);
  Analyzed.VLA =
      std::make_unique<VariableLookupAnalysis>(Analyzed.Diagnostics);
  Analyzed.VLA->runOnAST(*Analyzed.AST);
  return Analyzed;
}

const Expr *definitionValue(const Definition &Def,
                            const ParentMapAnalysis &PM) {
  if (!Def.syntax())
    return nullptr;

  const Node *BindingNode = PM.upTo(*Def.syntax(), Node::NK_Binding);
  if (!BindingNode)
    return nullptr;

  const auto &Binding = static_cast<const nixf::Binding &>(*BindingNode);
  return Binding.value().get();
}

std::optional<std::string> importedFile(const Expr &Value,
                                        const std::string &BaseFile) {
  const Expr *E = ignoreParens(&Value);
  if (!E || E->kind() != Node::NK_ExprCall)
    return std::nullopt;

  const auto &Call = static_cast<const ExprCall &>(*E);
  const Expr *Fn = ignoreParens(&Call.fn());
  if (!Fn || Fn->kind() != Node::NK_ExprVar)
    return std::nullopt;

  const auto &FnVar = static_cast<const ExprVar &>(*Fn);
  if (FnVar.id().name() != "import")
    return std::nullopt;

  if (Call.args().empty())
    return std::nullopt;

  const Expr *PathArg = ignoreParens(Call.args().front().get());
  if (!PathArg || PathArg->kind() != Node::NK_ExprPath)
    return std::nullopt;

  const auto &Path = static_cast<const ExprPath &>(*PathArg);
  if (!Path.parts().isLiteral())
    return std::nullopt;

  return resolveExprPath(BaseFile, Path.parts().literal());
}

std::optional<std::size_t> selectedAttrPathLength(const ExprSelect &Sel,
                                                  const Node &Target,
                                                  const ParentMapAnalysis &PM) {
  if (!Sel.path())
    return std::nullopt;

  const Node *UpAttrName = PM.upTo(Target, Node::NK_AttrName);
  if (!UpAttrName)
    return std::nullopt;

  const Node *UpAttrPath = PM.query(*UpAttrName);
  if (UpAttrPath != Sel.path())
    return std::nullopt;

  const auto &Names = Sel.path()->names();
  for (std::size_t Index = 0; Index < Names.size(); ++Index) {
    if (Names[Index].get() == UpAttrName)
      return Index + 1;
  }

  return std::nullopt;
}

std::optional<const Expr *> resolveExprVar(const Expr &E,
                                           const VariableLookupAnalysis &VLA,
                                           const ParentMapAnalysis &PM) {
  const Expr *Current = ignoreParens(&E);
  if (!Current || Current->kind() != Node::NK_ExprVar)
    return std::nullopt;

  const auto &Var = static_cast<const ExprVar &>(*Current);
  const Definition *Def;
  try {
    Def = &findVarDefinition(Var, VLA);
  } catch (const std::exception &E) {
    return std::nullopt;
  }

  const Expr *Value = definitionValue(*Def, PM);
  if (!Value)
    return std::nullopt;

  return Value;
}

std::optional<Location> defineStringListEntry(const ExprList &List,
                                              const std::string &Name,
                                              const AnalyzedFile &TU) {
  for (const auto &Element : List.elements()) {
    const Expr *E = ignoreParens(Element.get());
    if (!E || E->kind() != Node::NK_ExprString)
      continue;

    const auto &String = static_cast<const ExprString &>(*E);
    if (String.isLiteral() && String.literal() == Name) {
      return Location{
          .uri = URIForFile::canonicalize(TU.File, TU.File),
          .range = toLSPRange(TU.Src, String.range()),
      };
    }
  }

  return std::nullopt;
}

std::optional<Location> defineGeneratedAttr(const Expr &E,
                                            const std::string &Name,
                                            const AnalyzedFile &TU) {
  const Expr *Current = ignoreParens(&E);
  if (!Current || Current->kind() != Node::NK_ExprCall)
    return std::nullopt;

  const auto &Call = static_cast<const ExprCall &>(*Current);
  const Expr *Fn = ignoreParens(&Call.fn());
  if (!Fn || Fn->kind() != Node::NK_ExprSelect)
    return std::nullopt;

  const auto &FnSelect = static_cast<const ExprSelect &>(*Fn);
  if (!FnSelect.path() || FnSelect.path()->names().empty())
    return std::nullopt;

  const auto &FnName = FnSelect.path()->names().back();
  if (!FnName->isStatic() || FnName->staticName() != "mkPaths")
    return std::nullopt;

  if (Call.args().size() < 2)
    return std::nullopt;

  const Expr *NamesArg = ignoreParens(Call.args()[1].get());
  if (!NamesArg || NamesArg->kind() != Node::NK_ExprList)
    return std::nullopt;

  return defineStringListEntry(static_cast<const ExprList &>(*NamesArg), Name,
                               TU);
}

std::optional<Location>
defineAttrPathInExpr(const AttrPath &Path, const Expr &Root, AnalyzedFile &TU,
                     std::optional<std::size_t> Limit,
                     std::size_t StartIndex = 0, unsigned Depth = 0) {
  if (Depth > 8)
    return std::nullopt;

  const Expr *Current = &Root;
  const auto &Names = Path.names();
  const std::size_t EndIndex = Limit.value_or(Names.size());

  for (std::size_t Index = StartIndex; Index < EndIndex; ++Index) {
    const auto &Name = Names[Index];
    if (!Name->isStatic())
      return std::nullopt;

    Current = returnedExpr(Current);
    if (!Current)
      return std::nullopt;

    if (std::optional<Location> Loc =
            defineGeneratedAttr(*Current, Name->staticName(), TU))
      return Index == EndIndex - 1 ? Loc : std::nullopt;

    if (std::optional<const Expr *> Resolved =
            resolveExprVar(*Current, *TU.VLA, TU.PM))
      Current = *Resolved;

    Current = returnedExpr(Current);
    if (!Current || Current->kind() != Node::NK_ExprAttrs)
      return std::nullopt;

    const auto &Attrs = static_cast<const ExprAttrs &>(*Current);
    const auto &StaticAttrs = Attrs.sema().staticAttrs();
    auto It = StaticAttrs.find(Name->staticName());
    if (It == StaticAttrs.end())
      return std::nullopt;

    if (Index == EndIndex - 1) {
      if (It->second.fromInherit() && It->second.value()) {
        if (std::optional<const Expr *> Resolved =
                resolveExprVar(*It->second.value(), *TU.VLA, TU.PM)) {
          if (const Node *Binding = TU.PM.upTo(**Resolved, Node::NK_Binding)) {
            const auto &Path =
                static_cast<const nixf::Binding &>(*Binding).path();
            if (!Path.names().empty()) {
              return Location{
                  .uri = URIForFile::canonicalize(TU.File, TU.File),
                  .range = toLSPRange(TU.Src, Path.names().front()->range()),
              };
            }
          }
        }
      }
      return Location{
          .uri = URIForFile::canonicalize(TU.File, TU.File),
          .range = toLSPRange(TU.Src, It->second.key().range()),
      };
    }

    Current = It->second.value();
    if (!Current)
      return std::nullopt;

    if (std::optional<const Expr *> Resolved =
            resolveExprVar(*Current, *TU.VLA, TU.PM))
      Current = *Resolved;

    if (std::optional<std::string> ImportedFile =
            importedFile(*Current, TU.File)) {
      std::optional<AnalyzedFile> Imported = analyzeFile(*ImportedFile);
      if (!Imported)
        return std::nullopt;

      if (std::optional<Location> Loc = defineAttrPathInExpr(
              Path, *static_cast<const Expr *>(Imported->AST.get()), *Imported,
              Limit, Index + 1, Depth + 1))
        return Loc;
    }
  }

  return std::nullopt;
}

std::optional<Location> defineImportedSelect(const ExprSelect &Sel,
                                             const VariableLookupAnalysis &VLA,
                                             const ParentMapAnalysis &PM,
                                             const std::string &BaseFile,
                                             std::optional<std::size_t> Limit) {
  if (!Sel.path() || Sel.expr().kind() != Node::NK_ExprVar)
    return std::nullopt;

  const auto &Base = static_cast<const ExprVar &>(Sel.expr());
  const Definition *Def;
  try {
    Def = &findVarDefinition(Base, VLA);
  } catch (const std::exception &E) {
    return std::nullopt;
  }

  const Expr *Value = definitionValue(*Def, PM);
  if (!Value)
    return std::nullopt;

  std::optional<std::string> ImportedFile = importedFile(*Value, BaseFile);
  if (!ImportedFile)
    return std::nullopt;

  std::optional<AnalyzedFile> Imported = analyzeFile(*ImportedFile);
  if (!Imported)
    return std::nullopt;

  return defineAttrPathInExpr(*Sel.path(),
                              *static_cast<const Expr *>(Imported->AST.get()),
                              *Imported, Limit, 0, 1);
}

std::optional<std::vector<std::string>>
optionAttrPathScope(const Node &N, const ParentMapAnalysis &PM) {
  using PathResult = FindAttrPathResult;
  std::vector<std::string> Scope;
  auto R = findAttrPathForOptions(N, PM, Scope);
  if (R != PathResult::OK)
    return std::nullopt;
  return Scope;
}

/// \brief Get nixpkgs definition from a selector.
Locations defineNixpkgsSelector(const Selector &Sel,
                                AttrSetClient &NixpkgsClient) {
  try {
    // Ask nixpkgs provider information about this selector.
    NixpkgsDefinitionProvider NDP(NixpkgsClient);
    return NDP.resolveSelector(Sel);
  } catch (NoLocationsFoundInNixpkgsException &E) {
    elog("definition/idiom: {0}", E.what());
  } catch (WorkerReportedException &E) {
    elog("definition/idiom/worker: {0}", E.takeError());
  }
  return {};
}

/// \brief Get definiton of select expressions.
Locations defineSelect(const ExprSelect &Sel, const VariableLookupAnalysis &VLA,
                       const ParentMapAnalysis &PM,
                       AttrSetClient &NixpkgsClient,
                       const std::string &BaseFile,
                       std::optional<std::size_t> Limit) {
  if (std::optional<Location> Loc =
          defineImportedSelect(Sel, VLA, PM, BaseFile, Limit))
    return Locations{*Loc};

  // Currently we can only deal with idioms.
  // Maybe more data-flow analysis will be added though.
  try {
    return defineNixpkgsSelector(mkSelector(Sel, VLA, PM), NixpkgsClient);
  } catch (IdiomSelectorException &E) {
    elog("defintion/idiom/selector: {0}", E.what());
  }
  return {};
}

Locations defineVarStatic(const ExprVar &Var, const VariableLookupAnalysis &VLA,
                          const URIForFile &URI, llvm::StringRef Src) {
  const Definition &Def = findVarDefinition(Var, VLA);
  return {convertToLocation(Src, Def, URI)};
}

template <class T>
std::vector<T> mergeVec(std::vector<T> A, const std::vector<T> &B) {
  A.insert(A.end(), B.begin(), B.end());
  return A;
}

llvm::Expected<Locations>
defineVar(const ExprVar &Var, const VariableLookupAnalysis &VLA,
          const ParentMapAnalysis &PM, AttrSetClient &NixpkgsClient,
          const URIForFile &URI, llvm::StringRef Src) {
  try {
    Locations StaticLocs = defineVarStatic(Var, VLA, URI, Src);

    // Nixpkgs locations.
    try {
      Selector Sel = mkVarSelector(Var, VLA, PM);
      Locations NixpkgsLocs = defineNixpkgsSelector(Sel, NixpkgsClient);
      return mergeVec(std::move(StaticLocs), NixpkgsLocs);
    } catch (std::exception &E) {
      elog("definition/idiom/selector: {0}", E.what());
      return StaticLocs;
    }
  } catch (std::exception &E) {
    elog("definition/static: {0}", E.what());
    return Locations{};
  }
  return error("unreachable code! Please submit an issue");
}

/// \brief Squash a vector into smaller json variant.
template <class T> llvm::json::Value squash(std::vector<T> List) {
  std::size_t Size = List.size();
  switch (Size) {
  case 0:
    return nullptr;
  case 1:
    return std::move(List.back());
  default:
    break;
  }
  return std::move(List);
}

template <class T>
llvm::Expected<llvm::json::Value> squash(llvm::Expected<std::vector<T>> List) {
  if (!List)
    return List.takeError();
  return squash(std::move(*List));
}

} // namespace

const Definition &nixd::findDefinition(const Node &N,
                                       const ParentMapAnalysis &PMA,
                                       const VariableLookupAnalysis &VLA) {
  const ExprVar *Var = findVar(N, PMA, VLA);
  if (!Var) [[unlikely]] {
    if (const Definition *Def = findSelfDefinition(N, PMA, VLA))
      return *Def;
    throw CannotFindVarException();
  }
  assert(Var->kind() == Node::NK_ExprVar);
  return findVarDefinition(*Var, VLA);
}

void Controller::onDefinition(const TextDocumentPositionParams &Params,
                              Callback<llvm::json::Value> Reply) {
  using CheckTy = Locations;
  auto Action = [Reply = std::move(Reply), URI = Params.textDocument.uri,
                 Pos = toNixfPosition(Params.position), this]() mutable {
    const auto File = URI.file().str();
    return Reply(squash([&]() -> llvm::Expected<Locations> {
      const auto TU = CheckDefault(getTU(File));
      const auto AST = CheckDefault(getAST(*TU));
      const auto &VLA = *TU->variableLookup();
      const auto &PM = *TU->parentMap();
      const auto &N = *CheckDefault(AST->descend({Pos, Pos}));
      const auto &UpExpr = *CheckDefault(PM.upExpr(N));

      // Special case for inherited names.
      if (const ExprVar *Var = findInheritVar(N, PM, VLA))
        return defineVar(*Var, VLA, PM, *nixpkgsClient(), URI, TU->src());

      switch (UpExpr.kind()) {
      case Node::NK_ExprVar: {
        const auto &Var = static_cast<const ExprVar &>(UpExpr);
        return defineVar(Var, VLA, PM, *nixpkgsClient(), URI, TU->src());
      }
      case Node::NK_ExprSelect: {
        const auto &Sel = static_cast<const ExprSelect &>(UpExpr);
        return defineSelect(Sel, VLA, PM, *nixpkgsClient(), File,
                            selectedAttrPathLength(Sel, N, PM));
      }
      case Node::NK_ExprAttrs:
        if (std::optional<std::vector<std::string>> Scope =
                optionAttrPathScope(N, PM))
          return optionDeclarationLocations(*Scope);
        return Locations{};
      case Node::NK_ExprPath: {
        const auto &Path = static_cast<const ExprPath &>(UpExpr);
        if (auto Loc = definePath(Path, File))
          return Locations{*Loc};
        return Locations{};
      }
      default:
        break;
      }
      return error("unknown node type for definition");
    }()));
  };
  postToPool(std::move(Action));
}

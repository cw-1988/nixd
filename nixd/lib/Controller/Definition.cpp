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
#include "nixd/Controller/FlakeInputInspect.h"
#include "nixd/Controller/ModuleInputInspect.h"
#include "nixd/Protocol/AttrSet.h"

#include "lspserver/Protocol.h"

#include <boost/asio/post.hpp>

#include <llvm/Support/Error.h>
#include <llvm/Support/JSON.h>

#include <nixf/Basic/Nodes/Attrs.h>
#include <nixf/Basic/Nodes/Basic.h>
#include <nixf/Basic/Nodes/Expr.h>
#include <nixf/Basic/Nodes/Simple.h>
#include <nixf/Sema/ParentMap.h>
#include <nixf/Sema/VariableLookup.h>

#include <exception>
#include <filesystem>
#include <functional>
#include <semaphore>

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

std::optional<std::vector<std::string>>
optionAttrPathScope(const Node &N, const ParentMapAnalysis &PM) {
  using PathResult = FindAttrPathResult;
  std::vector<std::string> Scope;
  auto R = findAttrPathForOptions(N, PM, Scope);
  if (R != PathResult::OK)
    return std::nullopt;
  return Scope;
}

Locations defineModuleInputInspection(
    const ModuleInputInspectContext &Context, std::string_view File,
    const std::function<std::vector<ResolvedOptionField>(
        const std::vector<std::string> &, const std::string &)> &CompleteOptions,
    const ModuleInputAttrCompleter &CompleteAttrs) {
  std::string Content = renderModuleInputInspectionDocument(
      Context.Input, Context.Scope, Context.Sources, File, CompleteOptions,
      CompleteAttrs);
  std::filesystem::path Path =
      writeModuleInputInspectionFile(Context.Input, File, std::move(Content));

  const std::string PathStr = Path.string();
  return Locations{Location{
      .uri = URIForFile::canonicalize(PathStr, PathStr),
      .range = {{0, 0}, {0, 0}},
  }};
}

Locations defineFlakeInputInspection(const FlakeOutputInputContext &Context,
                                     std::string_view File) {
  std::string Content = renderFlakeOutputInputInspectionDocument(Context, File);
  std::filesystem::path Path =
      writeFlakeOutputInputInspectionFile(Context, File, std::move(Content));

  const std::string PathStr = Path.string();
  return Locations{Location{
      .uri = URIForFile::canonicalize(PathStr, PathStr),
      .range = {{0, 0}, {0, 0}},
  }};
}

std::optional<std::vector<std::string>>
nixpkgsScopeForModuleInput(std::string_view Input) {
  if (Input == "pkgs")
    return std::vector<std::string>{};
  if (Input == "lib")
    return std::vector<std::string>{"lib"};
  return std::nullopt;
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
                       AttrSetClient &NixpkgsClient) {
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

      if (std::optional<FlakeOutputInputContext> Context =
              findFlakeOutputInputContext(N, VLA, PM, File))
        return defineFlakeInputInspection(*Context, File);

      auto Resolve = [&](const std::vector<std::string> &Scope) {
        return resolveOptionInfosForFile(File, Scope);
      };
      if (std::optional<ModuleInputInspectContext> Context =
              findModuleInputInspectContext(N, VLA, PM, Resolve)) {
        auto Complete = [&](const std::vector<std::string> &Scope,
                            const std::string &Prefix) {
          return completeDerivedOptionsForFile(File, Scope, Prefix);
        };
        ModuleInputAttrCompleter CompleteAttrs;
        if (std::optional<std::vector<std::string>> NixpkgsScope =
                nixpkgsScopeForModuleInput(Context->Input)) {
          CompleteAttrs =
              [this, Root = std::move(*NixpkgsScope)](
                  const std::vector<std::string> &Scope,
                  const std::string &Prefix) mutable {
                std::vector<std::string> FullScope = Root;
                FullScope.insert(FullScope.end(), Scope.begin(), Scope.end());
                std::binary_semaphore Ready(0);
                std::vector<std::string> Names;
                auto OnReply =
                    [&Ready, &Names](
                        llvm::Expected<AttrPathCompleteResponse> Resp) {
                      if (Resp)
                        Names = std::move(*Resp);
                      else
                        consumeError(Resp.takeError());
                      Ready.release();
                    };
                nixpkgsClient()->attrpathComplete(
                    AttrPathCompleteParams{.Scope = std::move(FullScope),
                                           .Prefix = Prefix},
                    std::move(OnReply));
                Ready.acquire();
                return Names;
              };
        }
        return defineModuleInputInspection(*Context, File, Complete,
                                           CompleteAttrs);
      }

      // Special case for inherited names.
      if (const ExprVar *Var = findInheritVar(N, PM, VLA))
        return defineVar(*Var, VLA, PM, *nixpkgsClient(), URI, TU->src());

      const auto &UpExpr = *CheckDefault(PM.upExpr(N));

      switch (UpExpr.kind()) {
      case Node::NK_ExprVar: {
        const auto &Var = static_cast<const ExprVar &>(UpExpr);
        return defineVar(Var, VLA, PM, *nixpkgsClient(), URI, TU->src());
      }
      case Node::NK_ExprSelect: {
        const auto &Sel = static_cast<const ExprSelect &>(UpExpr);
        return defineSelect(Sel, VLA, PM, *nixpkgsClient());
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
      return Locations{};
    }()));
  };
  postToPool(std::move(Action));
}

/// \file
/// \brief Implementation of [Hover Request].
/// [Hover Request]:
/// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_hover

#include "AST.h"
#include "CheckReturn.h"
#include "Convert.h"
#include "Definition.h"
#include "PathResolve.h"

#include "nixd/Controller/Controller.h"
#include "nixd/Protocol/AttrSet.h"

#include <boost/asio/post.hpp>

#include <llvm/Support/Error.h>

#include <nixf/Basic/Nodes/Attrs.h>
#include <nixf/Basic/Nodes/Expr.h>
#include <nixf/Basic/Nodes/Lambda.h>
#include <nixf/Basic/Nodes/Simple.h>
#include <nixf/Parse/Parser.h>

#include <fstream>
#include <memory>
#include <optional>
#include <semaphore>
#include <sstream>
#include <string>
#include <vector>

using namespace nixd;
using namespace llvm::json;
using namespace nixf;
using namespace lspserver;

namespace {

class OptionsHoverProvider {
  AttrSetClient &Client;

public:
  OptionsHoverProvider(AttrSetClient &Client) : Client(Client) {}
  std::optional<OptionDescription>
  resolveHover(const std::vector<std::string> &Scope) {
    std::binary_semaphore Ready(0);
    std::optional<OptionDescription> Desc;
    auto OnReply = [&Ready, &Desc](llvm::Expected<OptionInfoResponse> Resp) {
      if (Resp)
        Desc = *Resp;
      else
        elog("options hover: {0}", Resp.takeError());
      Ready.release();
    };

    Client.optionInfo(Scope, std::move(OnReply));
    Ready.acquire();

    return Desc;
  }
};

/// \brief Provide package information, library information ... , from nixpkgs.
class NixpkgsHoverProvider {
  AttrSetClient &NixpkgsClient;

  /// \brief Make markdown documentation by package description
  ///
  /// FIXME: there are many markdown generation in language server.
  /// Maybe we can add structured generating first?
  static std::string mkMarkdown(const PackageDescription &Package) {
    std::ostringstream OS;
    // Make each field a new section

    if (Package.Name) {
      OS << "`" << *Package.Name << "`";
      OS << "\n";
    }

    // Make links to homepage.
    if (Package.Homepage) {
      OS << "[homepage](" << *Package.Homepage << ")";
      OS << "\n";
    }

    if (Package.Description) {
      OS << "## Description"
         << "\n\n";
      OS << *Package.Description;
      OS << "\n\n";

      if (Package.LongDescription) {
        OS << "\n\n";
        OS << *Package.LongDescription;
        OS << "\n\n";
      }
    }

    return OS.str();
  }

  /// \brief Make markdown including both package and value description
  static std::string mkMarkdown(const AttrPathInfoResponse &Info) {
    std::ostringstream OS;
    // Package section (if available)
    OS << mkMarkdown(Info.PackageDesc);

    // Value description section
    if (Info.ValueDesc) {
      const auto &VD = *Info.ValueDesc;
      if (!OS.str().empty())
        OS << "\n";
      if (!VD.Doc.empty()) {
        OS << VD.Doc << "\n\n";
      }
      if (VD.Arity != 0) {
        OS << "**Arity:** " << VD.Arity << "\n";
      }
      if (!VD.Args.empty()) {
        OS << "**Args:** ";
        for (size_t Idx = 0; Idx < VD.Args.size(); ++Idx) {
          OS << "`" << VD.Args[Idx] << "`";
          if (Idx + 1 < VD.Args.size())
            OS << ", ";
        }
        OS << "\n";
      }
    }

    return OS.str();
  }

public:
  NixpkgsHoverProvider(AttrSetClient &NixpkgsClient)
      : NixpkgsClient(NixpkgsClient) {}

  std::optional<std::string> resolveSelector(const nixd::Selector &Sel) {
    std::binary_semaphore Ready(0);
    std::optional<AttrPathInfoResponse> Info;
    auto OnReply = [&Ready, &Info](llvm::Expected<AttrPathInfoResponse> Resp) {
      if (Resp)
        Info = *Resp;
      else
        elog("nixpkgs provider: {0}", Resp.takeError());
      Ready.release();
    };
    NixpkgsClient.attrpathInfo(Sel, std::move(OnReply));
    Ready.acquire();

    if (!Info)
      return std::nullopt;

    return mkMarkdown(*Info);
  }
};

/// \brief Get nixpkgs hover info from a selector.
std::optional<Hover> hoverNixpkgsSelector(const Selector &Sel,
                                          const nixf::Node &N,
                                          const VariableLookupAnalysis &VLA,
                                          const ParentMapAnalysis &PM,
                                          AttrSetClient &NixpkgsClient,
                                          llvm::StringRef Src) {
  try {
    // Ask nixpkgs provider information about this selector.
    NixpkgsHoverProvider NHP(NixpkgsClient);
    if (std::optional<std::string> Doc = NHP.resolveSelector(Sel)) {
      return Hover{
          .contents =
              MarkupContent{
                  .kind = MarkupKind::Markdown,
                  .value = std::move(*Doc),
              },
          .range = toLSPRange(Src, N.range()),
      };
    }
  } catch (std::exception &E) {
    elog("hover/idiom: {0}", E.what());
  }
  return std::nullopt;
}

/// \brief Get hover info for ExprVar.
std::optional<Hover> hoverVar(const ExprVar &Var,
                              const VariableLookupAnalysis &VLA,
                              const ParentMapAnalysis &PM,
                              AttrSetClient &NixpkgsClient,
                              llvm::StringRef Src) {
  try {
    Selector Sel = idioms::mkVarSelector(Var, VLA, PM);
    return hoverNixpkgsSelector(Sel, Var, VLA, PM, NixpkgsClient, Src);
  } catch (std::exception &E) {
    elog("hover/idiom/selector: {0}", E.what());
  }
  return std::nullopt;
}

/// \brief Get hover info for ExprSelect.
std::optional<Hover> hoverSelect(const ExprSelect &Sel,
                                 const VariableLookupAnalysis &VLA,
                                 const ParentMapAnalysis &PM,
                                 AttrSetClient &NixpkgsClient,
                                 llvm::StringRef Src) {
  try {
    Selector S = idioms::mkSelector(Sel, VLA, PM);
    return hoverNixpkgsSelector(S, Sel, VLA, PM, NixpkgsClient, Src);
  } catch (std::exception &E) {
    elog("hover/idiom/selector: {0}", E.what());
  }
  return std::nullopt;
}

const Node *definitionPreviewNode(const Definition &Def,
                                  const ParentMapAnalysis &PM) {
  if (!Def.syntax())
    return nullptr;

  if (const Node *Binding = PM.upTo(*Def.syntax(), Node::NK_Binding))
    return Binding;

  return Def.syntax();
}

const Node *keyPreviewNode(const Node &Key, const ParentMapAnalysis &PM) {
  if (const Node *Binding = PM.upTo(Key, Node::NK_Binding))
    return Binding;

  return &Key;
}

std::string previewSource(const Node &Preview, llvm::StringRef Src) {
  std::string Text(Preview.src(Src));
  std::size_t Indent = static_cast<std::size_t>(Preview.lCur().column());
  if (Indent == 0)
    return Text;

  std::string Result;
  Result.reserve(Text.size());
  bool AtLineStart = false;
  for (std::size_t I = 0; I < Text.size();) {
    if (AtLineStart) {
      std::size_t Skipped = 0;
      while (Skipped < Indent && I < Text.size() && Text[I] == ' ') {
        ++I;
        ++Skipped;
      }
      AtLineStart = false;
      if (I >= Text.size())
        break;
    }

    char C = Text[I++];
    Result += C;
    if (C == '\n')
      AtLineStart = true;
  }

  return Result;
}

std::string fencedPreview(const Node &Preview, llvm::StringRef Src) {
  std::string Snippet = previewSource(Preview, Src);
  std::string Docs;
  Docs.reserve(Snippet.size() + 10);
  Docs += "```nix\n";
  Docs += Snippet;
  Docs += "\n```";
  return Docs;
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

struct PreviewContext {
  const std::string &File;
  llvm::StringRef Src;
  const VariableLookupAnalysis &VLA;
  const ParentMapAnalysis &PM;
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
  Analyzed.VLA = std::make_unique<VariableLookupAnalysis>(Analyzed.Diagnostics);
  Analyzed.VLA->runOnAST(*Analyzed.AST);
  return Analyzed;
}

std::optional<std::string> importedFile(const Expr &Value,
                                        const std::string &BaseFile) {
  const Expr *E = ignoreParens(&Value);
  if (!E || E->kind() != Node::NK_ExprCall)
    return std::nullopt;

  const auto &Call = static_cast<const ExprCall &>(*E);
  const Expr *Fn = ignoreParens(&Call.fn());
  if (Fn) {
    // Support the common `(import ./file.nix) { ... }` shape, parsed as a
    // call whose callee is itself an import call.
    if (std::optional<std::string> File = importedFile(*Fn, BaseFile))
      return File;
  }

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

std::optional<const Expr *> resolveExprVar(const Expr &E,
                                           const VariableLookupAnalysis &VLA,
                                           const ParentMapAnalysis &PM) {
  const Expr *Current = ignoreParens(&E);
  if (!Current || Current->kind() != Node::NK_ExprVar)
    return std::nullopt;

  const Definition *Def;
  try {
    Def = &findDefinition(*Current, PM, VLA);
  } catch (const std::exception &E) {
    return std::nullopt;
  }

  const Expr *Value = definitionValue(*Def, PM);
  if (!Value)
    return std::nullopt;

  return Value;
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

const AttrName *selectedAttrName(const ExprSelect &Sel, const Node &Target,
                                 const ParentMapAnalysis &PM) {
  if (!Sel.path())
    return nullptr;

  const Node *UpAttrName = PM.upTo(Target, Node::NK_AttrName);
  if (!UpAttrName)
    return nullptr;

  if (PM.query(*UpAttrName) != Sel.path())
    return nullptr;

  return static_cast<const AttrName *>(UpAttrName);
}

std::optional<std::string>
previewAttrPathInExpr(const AttrPath &Path, const Expr &Root,
                      const PreviewContext &TU,
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

    if (std::optional<const Expr *> Resolved =
            resolveExprVar(*Current, TU.VLA, TU.PM))
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
                resolveExprVar(*It->second.value(), TU.VLA, TU.PM)) {
          if (const Node *Binding = TU.PM.upTo(**Resolved, Node::NK_Binding))
            return previewSource(*Binding, TU.Src);
          return previewSource(**Resolved, TU.Src);
        }
      }
      return previewSource(*keyPreviewNode(It->second.key(), TU.PM), TU.Src);
    }

    Current = It->second.value();
    if (!Current)
      return std::nullopt;

    if (std::optional<const Expr *> Resolved =
            resolveExprVar(*Current, TU.VLA, TU.PM))
      Current = *Resolved;

    if (std::optional<std::string> ImportedFile =
            importedFile(*Current, TU.File)) {
      std::optional<AnalyzedFile> Imported = analyzeFile(*ImportedFile);
      if (!Imported)
        return std::nullopt;

      PreviewContext ImportedContext{Imported->File, Imported->Src,
                                     *Imported->VLA, Imported->PM};
      if (std::optional<std::string> Preview = previewAttrPathInExpr(
              Path, *static_cast<const Expr *>(Imported->AST.get()),
              ImportedContext, Limit, Index + 1, Depth + 1))
        return Preview;
    }
  }

  return std::nullopt;
}

std::optional<std::string>
previewImportedSelect(const ExprSelect &Sel, const Definition &Def,
                      const VariableLookupAnalysis &VLA,
                      const ParentMapAnalysis &PM, llvm::StringRef Src,
                      const std::string &BaseFile, std::size_t Limit) {
  if (!Sel.path())
    return std::nullopt;

  const Expr *Value = definitionValue(Def, PM);
  if (!Value)
    return std::nullopt;

  if (std::optional<std::string> ImportedPath =
          importedFile(*Value, BaseFile)) {
    std::optional<AnalyzedFile> Imported = analyzeFile(*ImportedPath);
    if (!Imported)
      return std::nullopt;

    PreviewContext ImportedContext{Imported->File, Imported->Src,
                                   *Imported->VLA, Imported->PM};
    return previewAttrPathInExpr(
        *Sel.path(), *static_cast<const Expr *>(Imported->AST.get()),
        ImportedContext, Limit);
  }

  PreviewContext Current{BaseFile, Src, VLA, PM};
  return previewAttrPathInExpr(*Sel.path(), *Value, Current, Limit);
}

std::optional<Hover> hoverStaticPreview(const Node &RangeNode,
                                        const Node &Preview,
                                        llvm::StringRef Src) {
  return Hover{
      .contents =
          MarkupContent{
              .kind = MarkupKind::Markdown,
              .value = fencedPreview(Preview, Src),
          },
      .range = toLSPRange(Src, RangeNode.range()),
  };
}

std::optional<Hover> hoverVarStatic(const ExprVar &Var,
                                    const VariableLookupAnalysis &VLA,
                                    const ParentMapAnalysis &PM,
                                    llvm::StringRef Src) {
  try {
    const Definition &Def = findDefinition(Var, PM, VLA);
    if (Def.source() == Definition::DS_Builtin ||
        Def.source() == Definition::DS_With)
      return std::nullopt;

    const Node *Preview = definitionPreviewNode(Def, PM);
    if (!Preview)
      return std::nullopt;

    return hoverStaticPreview(Var, *Preview, Src);
  } catch (std::exception &E) {
    elog("hover/static: {0}", E.what());
  }
  return std::nullopt;
}

std::optional<Hover> hoverSelectStatic(const ExprSelect &Sel,
                                       const Node &Target,
                                       const VariableLookupAnalysis &VLA,
                                       const ParentMapAnalysis &PM,
                                       llvm::StringRef Src,
                                       const std::string &BaseFile) {
  try {
    if (Sel.expr().kind() != Node::NK_ExprVar)
      return std::nullopt;

    std::optional<std::size_t> Limit = selectedAttrPathLength(Sel, Target, PM);
    if (!Limit)
      return std::nullopt;

    const AttrName *RangeNode = selectedAttrName(Sel, Target, PM);
    if (!RangeNode)
      return std::nullopt;

    const Definition &Def = findDefinition(Sel.expr(), PM, VLA);
    if (Def.source() == Definition::DS_Builtin ||
        Def.source() == Definition::DS_With)
      return std::nullopt;

    std::optional<std::string> Snippet =
        previewImportedSelect(Sel, Def, VLA, PM, Src, BaseFile, *Limit);
    if (!Snippet)
      return std::nullopt;

    std::string Docs;
    Docs.reserve(Snippet->size() + 10);
    Docs += "```nix\n";
    Docs += *Snippet;
    Docs += "\n```";

    return Hover{
        .contents =
            MarkupContent{
                .kind = MarkupKind::Markdown,
                .value = std::move(Docs),
            },
        .range = toLSPRange(Src, RangeNode->range()),
    };
  } catch (std::exception &E) {
    elog("hover/static/select: {0}", E.what());
  }
  return std::nullopt;
}

} // namespace

void Controller::onHover(const TextDocumentPositionParams &Params,
                         Callback<std::optional<Hover>> Reply) {
  using CheckTy = std::optional<Hover>;
  auto Action = [Reply = std::move(Reply),
                 File = std::string(Params.textDocument.uri.file()),
                 RawPos = Params.position, this]() mutable {
    return Reply([&]() -> llvm::Expected<CheckTy> {
      const auto TU = CheckDefault(getTU(File));
      const auto AST = CheckDefault(getAST(*TU));
      const auto Pos = nixf::Position{RawPos.line, RawPos.character};
      const auto &N = *CheckDefault(AST->descend({Pos, Pos}));

      const auto Name = std::string(N.name());
      const auto &VLA = *TU->variableLookup();
      const auto &PM = *TU->parentMap();

      const auto &UpExpr = *CheckDefault(PM.upExpr(N));

      // Try to get hover info from nixpkgs.
      if (auto *Client = nixpkgsClient(); Client) {
        switch (UpExpr.kind()) {
        case Node::NK_ExprVar: {
          const auto &Var = static_cast<const ExprVar &>(UpExpr);
          if (auto H = hoverVar(Var, VLA, PM, *Client, TU->src()))
            return *H;
          if (auto H = hoverVarStatic(Var, VLA, PM, TU->src()))
            return *H;
          break;
        }
        case Node::NK_ExprSelect: {
          const auto &Sel = static_cast<const ExprSelect &>(UpExpr);
          if (auto H = hoverSelect(Sel, VLA, PM, *Client, TU->src()))
            return *H;
          if (auto H = hoverSelectStatic(Sel, N, VLA, PM, TU->src(), File))
            return *H;
          break;
        }
        case Node::NK_ExprAttrs: {
          // Try to get hover info from options.
          auto Scope = std::vector<std::string>();
          const auto R = findAttrPathForOptions(N, PM, Scope);
          if (R == FindAttrPathResult::OK) {
            for (const ResolvedOptionInfo &Info : resolveOptionInfos(Scope)) {
              const OptionDescription &Desc = Info.Description;
              std::string Docs;
              if (Desc.Type) {
                std::string TypeName = Desc.Type->Name.value_or("");
                std::string TypeDesc = Desc.Type->Description.value_or("");
                Docs += llvm::formatv("{0} ({1})", TypeName, TypeDesc);
              } else {
                Docs += "? (missing type)";
              }
              if (Desc.Description)
                Docs += "\n\n" + Desc.Description.value_or("");
              return Hover{
                  .contents =
                      MarkupContent{
                          .kind = MarkupKind::Markdown,
                          .value = std::move(Docs),
                      },
                  .range = toLSPRange(TU->src(), N.range()),
              };
            }
          }
          break;
        }
        default:
          break;
        }
      } else {
        switch (UpExpr.kind()) {
        case Node::NK_ExprVar: {
          const auto &Var = static_cast<const ExprVar &>(UpExpr);
          if (auto H = hoverVarStatic(Var, VLA, PM, TU->src()))
            return *H;
          break;
        }
        case Node::NK_ExprSelect: {
          const auto &Sel = static_cast<const ExprSelect &>(UpExpr);
          if (auto H = hoverSelectStatic(Sel, N, VLA, PM, TU->src(), File))
            return *H;
          break;
        }
        default:
          break;
        }
      }

      return std::nullopt;
    }());
  };
  postToPool(std::move(Action));
}

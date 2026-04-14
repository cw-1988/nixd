/// \file
/// \brief Implementation of [Code Completion].
/// [Code Completion]:
/// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_completion

#include "AST.h"
#include "CheckReturn.h"
#include "CompletionOptions.h"
#include "Convert.h"

#include "lspserver/Protocol.h"
#include "lspserver/SourceCode.h"

#include "nixd/Controller/Controller.h"
#include "nixd/Controller/Option.h"
#include "nixd/Protocol/AttrSet.h"

#include <nixf/Sema/VariableLookup.h>

#include <boost/asio/post.hpp>

#include <cctype>
#include <optional>
#include <semaphore>
#include <string_view>
#include <utility>

using namespace nixd;
using namespace lspserver;
using namespace nixf;

namespace {

using completion::addItem;
using completion::ExceedSizeError;

bool isWhitespace(char C) {
  return C == ' ' || C == '\t' || C == '\n' || C == '\r';
}

const Node *findCompletionNode(const Node &AST, std::string_view Src,
                               nixf::Position Pos) {
  if (const Node *Desc = AST.descend({Pos, Pos}))
    return Desc;

  lspserver::Position LSPPos{.line = Pos.line(), .character = Pos.column()};
  llvm::Expected<size_t> Offset =
      lspserver::positionToOffset(Src, LSPPos, true);
  if (!Offset) {
    llvm::consumeError(Offset.takeError());
    return nullptr;
  }

  for (size_t I = *Offset; I > 0; --I) {
    const size_t Prev = I - 1;
    if (isWhitespace(Src[Prev]))
      continue;
    const lspserver::Position PrevPos = lspserver::offsetToPosition(Src, Prev);
    return AST.descend({nixf::Position(PrevPos.line, PrevPos.character),
                        nixf::Position(PrevPos.line, PrevPos.character)});
  }
  return nullptr;
}

class VLACompletionProvider {
  const VariableLookupAnalysis &VLA;

  static CompletionItemKind getCompletionItemKind(const Definition &Def) {
    if (Def.isBuiltin()) {
      return CompletionItemKind::Keyword;
    }
    return CompletionItemKind::Variable;
  }

  /// Collect definition on some env, and also it's ancestors.
  void collectDef(std::vector<CompletionItem> &Items, const EnvNode *Env,
                  const std::string &Prefix) {
    if (!Env)
      return;
    collectDef(Items, Env->parent(), Prefix);
    for (const auto &[Name, Def] : Env->defs()) {
      if (Name.starts_with(
              "__")) // These names are nix internal implementation, skip.
        continue;
      assert(Def);
      if (Name.starts_with(Prefix)) {
        addItem(Items, CompletionItem{
                           .label = Name,
                           .kind = getCompletionItemKind(*Def),
                       });
      }
    }
  }

public:
  VLACompletionProvider(const VariableLookupAnalysis &VLA) : VLA(VLA) {}

  /// Perform code completion right after this node.
  void complete(const nixf::ExprVar &Desc, std::vector<CompletionItem> &Items,
                const ParentMapAnalysis &PM) {
    std::string Prefix = Desc.id().name();
    collectDef(Items, upEnv(Desc, VLA, PM), Prefix);
  }
};

/// \brief Provide completions by IPC. Asking nixpkgs provider.
/// We simply select nixpkgs in separate process, thus this value does not need
/// to be cached. (It is already cached in separate process.)
///
/// Currently, this procedure is explicitly blocked (synchronized). Because
/// query nixpkgs value is relatively fast. In the future there might be nixd
/// index, for performance.
class NixpkgsCompletionProvider {

  AttrSetClient &NixpkgsClient;

public:
  NixpkgsCompletionProvider(AttrSetClient &NixpkgsClient)
      : NixpkgsClient(NixpkgsClient) {}

  void resolvePackage(std::vector<std::string> Scope, std::string Name,
                      CompletionItem &Item) {
    std::binary_semaphore Ready(0);
    AttrPathInfoResponse Desc;
    auto OnReply = [&Ready, &Desc](llvm::Expected<AttrPathInfoResponse> Resp) {
      if (Resp)
        Desc = *Resp;
      Ready.release();
    };
    Scope.emplace_back(std::move(Name));
    NixpkgsClient.attrpathInfo(Scope, std::move(OnReply));
    Ready.acquire();
    // Format "detail" and document.
    const PackageDescription &PD = Desc.PackageDesc;
    Item.documentation = MarkupContent{
        .kind = MarkupKind::Markdown,
        .value = PD.Description.value_or("") + "\n\n" +
                 PD.LongDescription.value_or(""),
    };
    Item.detail = PD.Version.value_or("?");
  }

  /// \brief Ask nixpkgs provider, give us a list of names. (thunks)
  void completePackages(const AttrPathCompleteParams &Params,
                        std::vector<CompletionItem> &Items) {
    std::binary_semaphore Ready(0);
    std::vector<std::string> Names;
    auto OnReply = [&Ready,
                    &Names](llvm::Expected<AttrPathCompleteResponse> Resp) {
      if (!Resp) {
        lspserver::elog("nixpkgs evaluator reported: {0}", Resp.takeError());
        Ready.release();
        return;
      }
      Names = *Resp; // Copy response to waiting thread.
      Ready.release();
    };
    // Send request.
    NixpkgsClient.attrpathComplete(Params, std::move(OnReply));
    Ready.acquire();
    // Now we have "Names", use these to fill "Items".
    for (const auto &Name : Names) {
      if (Name.starts_with(Params.Prefix)) {
        addItem(Items, CompletionItem{
                           .label = Name,
                           .kind = CompletionItemKind::Field,
                           .data = llvm::formatv("{0}", toJSON(Params)),
                       });
      }
    }
  }
};

AttrPathCompleteParams mkParams(nixd::Selector Sel, bool IsComplete) {
  if (IsComplete || Sel.empty()) {
    return {
        .Scope = std::move(Sel),
        .Prefix = "",
    };
  }
  std::string Back = std::move(Sel.back());
  Sel.pop_back();
  return {
      .Scope = Sel,
      .Prefix = std::move(Back),
  };
}

#define DBG DBGPREFIX ": "

void completeVarName(const VariableLookupAnalysis &VLA,
                     const ParentMapAnalysis &PM, const nixf::ExprVar &N,
                     AttrSetClient &Client, std::vector<CompletionItem> &List) {
#define DBGPREFIX "completion/var"

  VLACompletionProvider VLAP(VLA);
  VLAP.complete(N, List, PM);

  // Try to complete the name by known idioms.
  try {
    Selector Sel = idioms::mkVarSelector(N, VLA, PM);

    // Clickling "pkgs" does not make sense for variable completion
    if (Sel.empty())
      return;

    // Invoke nixpkgs provider to get the completion list.
    NixpkgsCompletionProvider NCP(Client);
    // Variable names are always incomplete.
    NCP.completePackages(mkParams(Sel, /*IsComplete=*/false), List);
  } catch (ExceedSizeError &) {
    // Let "onCompletion" catch this exception to set "inComplete" field.
    throw;
  } catch (std::exception &E) {
    return log(DBG "skipped, reason: {0}", E.what());
  }

#undef DBGPREFIX
}

/// \brief Complete a "select" expression.
/// \param IsComplete Whether or not the last element of the selector is
/// effectively incomplete.
/// e.g.
///      - incomplete: `lib.gen|`
///      - complete:   `lib.attrset.|`
void completeSelect(const nixf::ExprSelect &Select, AttrSetClient &Client,
                    const nixf::VariableLookupAnalysis &VLA,
                    const nixf::ParentMapAnalysis &PM, bool IsComplete,
                    std::vector<CompletionItem> &List) {
#define DBGPREFIX "completion/select"
  // The base expr for selecting.
  const nixf::Expr &BaseExpr = Select.expr();

  // Determine that the name is one of special names interesting
  // for nix language. If it is not a simple variable, skip this
  // case.
  if (BaseExpr.kind() != Node::NK_ExprVar) {
    return;
  }

  const auto &Var = static_cast<const nixf::ExprVar &>(BaseExpr);
  // Ask nixpkgs provider to get idioms completion.
  NixpkgsCompletionProvider NCP(Client);

  try {
    Selector Sel =
        idioms::mkSelector(Select, idioms::mkVarSelector(Var, VLA, PM));
    NCP.completePackages(mkParams(Sel, IsComplete), List);
  } catch (ExceedSizeError &) {
    // Let "onCompletion" catch this exception to set "inComplete" field.
    throw;
  } catch (std::exception &E) {
    return log(DBG "skipped, reason: {0}", E.what());
  }

#undef DBGPREFIX
}

} // namespace

void Controller::onCompletion(const CompletionParams &Params,
                              Callback<CompletionList> Reply) {
  using CheckTy = CompletionList;
  auto Action = [Reply = std::move(Reply), URI = Params.textDocument.uri,
                 Pos = toNixfPosition(Params.position), this]() mutable {
    const auto File = URI.file().str();
    return Reply([&]() -> llvm::Expected<CompletionList> {
      const auto TU = CheckDefault(getTU(File));
      const auto AST = CheckDefault(getAST(*TU));

      const auto *Desc = findCompletionNode(*AST, TU->src(), Pos);
      CheckDefault(Desc);

      const auto &N = *Desc;
      const auto &PM = *TU->parentMap();

      return [&]() {
        CompletionList List;
        const VariableLookupAnalysis &VLA = *TU->variableLookup();
        try {
          if (std::optional<OptionValueContext> Context =
                  findOptionValueContext(N, PM, Pos)) {
            completion::completeOptionValue(
                *Context, resolveDerivedOptionInfos(Context->Scope), TU->src(),
                List.items);
            if (!List.items.empty())
              return List;
          }

          const auto &UpExpr = *CheckDefault(PM.upExpr(N));
          switch (UpExpr.kind()) {
          // In these cases, assume the cursor have "variable" scoping.
          case Node::NK_ExprVar: {
            completeVarName(VLA, PM, static_cast<const nixf::ExprVar &>(UpExpr),
                            *nixpkgsClient(), List.items);
            return List;
          }
          // A "select" expression. e.g.
          // foo.a|
          // foo.|
          // foo.a.bar|
          case Node::NK_ExprSelect: {
            const auto &Select = static_cast<const nixf::ExprSelect &>(UpExpr);
            completeSelect(Select, *nixpkgsClient(), VLA, PM,
                           N.kind() == Node::NK_Dot, List.items);
            return List;
          }
          case Node::NK_ExprAttrs: {
            if (std::optional<AttrPathCompleteParams> Params =
                    completion::optionAttrPathCompletionParams(N, PM)) {
              completion::completeOptionNames(
                  completeDerivedOptions(Params->Scope, Params->Prefix),
                  ClientCaps.CompletionSnippets, List.items);
            }
            return List;
          }
          default:
            return List;
          }
        } catch (ExceedSizeError &Err) {
          List.isIncomplete = true;
          return List;
        }
      }();
    }());
  };
  boost::asio::post(Pool, std::move(Action));
}

void Controller::onCompletionItemResolve(const CompletionItem &Params,
                                         Callback<CompletionItem> Reply) {

  auto Action = [Params, Reply = std::move(Reply), this]() mutable {
    if (Params.data.empty()) {
      Reply(Params);
      return;
    }
    AttrPathCompleteParams Req;
    auto EV = llvm::json::parse(Params.data);
    if (!EV) {
      // If the json value cannot be parsed, this is very unlikely to happen.
      Reply(EV.takeError());
      return;
    }

    llvm::json::Path::Root Root;
    fromJSON(*EV, Req, Root);

    // FIXME: handle null nixpkgsClient()
    NixpkgsCompletionProvider NCP(*nixpkgsClient());
    CompletionItem Resp = Params;
    NCP.resolvePackage(Req.Scope, Params.label, Resp);

    Reply(std::move(Resp));
  };
  boost::asio::post(Pool, std::move(Action));
}

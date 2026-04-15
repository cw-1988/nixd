/// \file
/// \brief Implementation of [Code Completion].
/// [Code Completion]:
/// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_completion

#include "CheckReturn.h"
#include "Completion/Context.h"
#include "Completion/Nixpkgs.h"
#include "Completion/Options.h"
#include "Completion/Select.h"
#include "Completion/Variables.h"
#include "Convert.h"
#include "Option/FlakeSchema.h"

#include "lspserver/Protocol.h"

#include "nixd/Controller/Controller.h"
#include "nixd/Controller/Option.h"

#include <nixf/Basic/Nodes/Expr.h>
#include <nixf/Sema/VariableLookup.h>

#include <boost/asio/post.hpp>

#include <optional>
#include <utility>

using namespace nixd;
using namespace lspserver;
using namespace nixf;

using completion::ExceedSizeError;

void Controller::onCompletion(const CompletionParams &Params,
                              Callback<CompletionList> Reply) {
  using CheckTy = CompletionList;
  auto Action = [Reply = std::move(Reply), URI = Params.textDocument.uri,
                 Pos = toNixfPosition(Params.position), this]() mutable {
    const auto File = URI.file().str();
    const bool UsesFlakeSchema = flake_schema::isFlakeFile(File);
    return Reply([&]() -> llvm::Expected<CompletionList> {
      const auto TU = CheckDefault(getTU(File));
      const auto AST = CheckDefault(getAST(*TU));

      const auto *Desc = completion::findCompletionNode(*AST, TU->src(), Pos);
      CheckDefault(Desc);

      const auto &N = *Desc;
      const auto &PM = *TU->parentMap();

      return [&]() {
        CompletionList List;
        const VariableLookupAnalysis &VLA = *TU->variableLookup();
        try {
          const auto &UpExpr = *CheckDefault(PM.upExpr(N));
          const bool InFlakeOutputsBody =
              UsesFlakeSchema && flake_schema::isInsideOutputsBody(N, PM);

          if (UpExpr.kind() == Node::NK_ExprAttrs) {
            if (std::optional<AttrPathCompleteParams> Params =
                    completion::optionAttrPathCompletionParams(N, PM)) {
              std::vector<std::string> Scope =
                  InFlakeOutputsBody
                      ? flake_schema::outputsBodyScope(Params->Scope)
                      : Params->Scope;
              if (UsesFlakeSchema || waitForOptionProvidersReadyForTests())
                completion::completeOptionNames(
                    completeDerivedOptionsForFile(File, Scope, Params->Prefix),
                    ClientCaps.CompletionSnippets, List.items);
              if (!List.items.empty())
                return List;
            }
          }

          if (std::optional<OptionValueContext> Context =
                  findOptionValueContext(N, PM, Pos)) {
            OptionValueContext ValueContext = *Context;
            if (InFlakeOutputsBody)
              ValueContext.Scope =
                  flake_schema::outputsBodyScope(ValueContext.Scope);
            if (UsesFlakeSchema || waitForOptionProvidersReadyForTests())
              completion::completeOptionValue(
                  ValueContext,
                  resolveDerivedOptionInfosForFile(File, ValueContext.Scope),
                  ClientCaps.CompletionSnippets, nixpkgsClient(), TU->src(),
                  List.items);
            if (!List.items.empty())
              return List;
          }

          switch (UpExpr.kind()) {
          // In these cases, assume the cursor have "variable" scoping.
          case Node::NK_ExprVar: {
            completion::completeVarName(
                VLA, PM, static_cast<const nixf::ExprVar &>(UpExpr),
                *nixpkgsClient(), List.items);
            return List;
          }
          // A "select" expression. e.g.
          // foo.a|
          // foo.|
          // foo.a.bar|
          case Node::NK_ExprSelect: {
            const auto &Select = static_cast<const nixf::ExprSelect &>(UpExpr);
            completion::completeSelect(Select, *nixpkgsClient(), VLA, PM,
                                       N.kind() == Node::NK_Dot, List.items);
            return List;
          }
          case Node::NK_ExprAttrs: {
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
  postToPool(std::move(Action));
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
    completion::NixpkgsCompletionProvider NCP(*nixpkgsClient());
    CompletionItem Resp = Params;
    NCP.resolvePackage(Req.Scope, Params.label, Resp);

    Reply(std::move(Resp));
  };
  postToPool(std::move(Action));
}

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
          std::optional<OptionValueContext> ValueContext =
              findOptionValueContext(N, PM, Pos);
          if (ValueContext) {
            if (waitForOptionProvidersReadyForTests())
              completion::completeOptionValue(
                  *ValueContext, resolveDerivedOptionInfos(ValueContext->Scope),
                  ClientCaps.CompletionSnippets, nixpkgsClient(), TU->src(),
                  List.items);
            if (!List.items.empty())
              return List;
          }

          if (std::optional<AttrPathCompleteParams> Params =
                  completion::optionAttrPathCompletionParams(N, PM, Pos,
                                                             TU->src())) {
            // An incomplete name immediately before another binding can make
            // parser recovery attach the cursor to the root attrset. The
            // enclosing option value still provides the correct schema scope.
            if (Params->Scope.empty() && ValueContext &&
                !ValueContext->Scope.empty())
              Params->Scope = ValueContext->Scope;
            if (waitForOptionProvidersReadyForTests())
              completion::completeOptionNames(
                  completeDerivedOptions(Params->Scope, Params->Prefix),
                  ClientCaps.CompletionSnippets, List.items);
            if (!List.items.empty())
              return List;
          }

          const auto &UpExpr = *CheckDefault(PM.upExpr(N));
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

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
#include "lspserver/SourceCode.h"

#include "nixd/Controller/Controller.h"
#include "nixd/Controller/Option.h"

#include <nixf/Basic/Nodes/Expr.h>
#include <nixf/Sema/VariableLookup.h>

#include <boost/asio/post.hpp>

#include <optional>
#include <set>
#include <utility>

using namespace nixd;
using namespace lspserver;
using namespace nixf;

using completion::ExceedSizeError;

namespace {

bool isWhitespace(char C) {
  return C == ' ' || C == '\t' || C == '\n' || C == '\r';
}

std::set<std::string> usedOptionNames(const ExprAttrs &Attrs) {
  std::set<std::string> Used;
  const Binds *Body = Attrs.binds();
  if (!Body)
    return Used;

  for (const std::shared_ptr<Node> &Entry : Body->bindings()) {
    if (!Entry)
      continue;

    if (Entry->kind() == Node::NK_Binding) {
      const auto &Binding = static_cast<const nixf::Binding &>(*Entry);
      const auto &Names = Binding.path().names();
      if (!Names.empty() && Names.front() && Names.front()->isStatic())
        Used.insert(Names.front()->staticName());
      continue;
    }

    if (Entry->kind() == Node::NK_Inherit) {
      const auto &Inherit = static_cast<const nixf::Inherit &>(*Entry);
      for (const std::shared_ptr<AttrName> &Name : Inherit.names()) {
        if (Name && Name->isStatic())
          Used.insert(Name->staticName());
      }
    }
  }

  return Used;
}

std::vector<ResolvedOptionField>
filterUsedOptionNames(std::vector<ResolvedOptionField> Fields,
                      const std::set<std::string> &Used) {
  if (Used.empty())
    return Fields;

  std::erase_if(Fields, [&](const ResolvedOptionField &Resolved) {
    return Used.contains(Resolved.Field.Name);
  });
  return Fields;
}

struct EnclosingOptionAttrSet {
  const ExprAttrs *Attrs = nullptr;
  std::vector<std::string> Scope;
};

std::optional<EnclosingOptionAttrSet>
attrSetFromNode(const Node *Current, const ParentMapAnalysis &PM,
                const OptionInfoResolver &Resolve) {
  while (Current) {
    if (Current->kind() == Node::NK_ExprAttrs) {
      const Node *Parent = PM.query(*Current);
      if (Parent && Parent->kind() == Node::NK_Binding) {
        const auto &AttrBinding = static_cast<const nixf::Binding &>(*Parent);
        if (AttrBinding.value().get() == Current)
          if (std::optional<SemanticOptionBinding> Semantic =
                  findSemanticOptionBinding(AttrBinding, PM, Resolve))
            return EnclosingOptionAttrSet{
                .Attrs = &static_cast<const ExprAttrs &>(*Current),
                .Scope = std::move(Semantic->Scope),
            };
      }
    }

    if (PM.isRoot(*Current))
      break;
    Current = PM.query(*Current);
  }
  return std::nullopt;
}

std::optional<EnclosingOptionAttrSet>
enclosingOptionAttrSetScope(const Node &AST, std::string_view Src,
                            nixf::Position Pos, const ParentMapAnalysis &PM,
                            const OptionInfoResolver &Resolve) {
  if (const Node *Desc = AST.descend({Pos, Pos}))
    if (std::optional<EnclosingOptionAttrSet> Context =
            attrSetFromNode(Desc, PM, Resolve))
      return Context;

  lspserver::Position LSPPos{.line = Pos.line(), .character = Pos.column()};
  llvm::Expected<size_t> Offset =
      lspserver::positionToOffset(Src, LSPPos, true);
  if (!Offset) {
    llvm::consumeError(Offset.takeError());
    return std::nullopt;
  }

  for (size_t I = *Offset; I > 0; --I) {
    const size_t Prev = I - 1;
    if (isWhitespace(Src[Prev]))
      continue;
    const lspserver::Position PrevPos = lspserver::offsetToPosition(Src, Prev);
    if (const Node *Desc =
            AST.descend({nixf::Position(PrevPos.line, PrevPos.character),
                         Pos})) {
      if (std::optional<EnclosingOptionAttrSet> Context =
              attrSetFromNode(Desc, PM, Resolve))
        return Context;
    }
  }

  return std::nullopt;
}

} // namespace

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
          const bool OptionsReady =
              UsesFlakeSchema || waitForOptionProvidersReadyForTests();
          const bool InFlakeOutputsBody =
              UsesFlakeSchema && flake_schema::isInsideOutputsBody(N, PM);
          auto Resolve = [&](const std::vector<std::string> &Scope) {
            if (UsesFlakeSchema) {
              std::vector<ResolvedOptionInfo> Infos =
                  flake_schema::resolveDerived(Scope);
              if (!Infos.empty())
                return Infos;
              return flake_schema::resolveDerived(
                  flake_schema::outputsBodyScope(Scope));
            }
            if (!OptionsReady)
              return std::vector<ResolvedOptionInfo>{};
            return resolveDerivedOptionInfosForFile(File, Scope);
          };
          const Node *UpExpr = PM.upExpr(N);
          const std::optional<EnclosingOptionAttrSet> CurrentAttrSet =
              attrSetFromNode(&N, PM, Resolve);

          if (UpExpr && UpExpr->kind() == Node::NK_ExprAttrs) {
            if (std::optional<AttrPathCompleteParams> Params =
                    completion::optionAttrPathCompletionParams(N, PM)) {
              std::vector<std::string> Scope =
                  InFlakeOutputsBody
                      ? flake_schema::outputsBodyScope(Params->Scope)
                      : Params->Scope;
              if (OptionsReady)
                completion::completeOptionNames(
                    filterUsedOptionNames(
                        completeDerivedOptionsForFile(File, Scope,
                                                      Params->Prefix),
                        CurrentAttrSet ? usedOptionNames(*CurrentAttrSet->Attrs)
                                       : std::set<std::string>{}),
                    ClientCaps.CompletionSnippets, List.items);
              if (!List.items.empty())
                return List;
            }
          }

          if (!UpExpr || UpExpr->kind() == Node::NK_ExprAttrs) {
            if (std::optional<EnclosingOptionAttrSet> Context =
                    enclosingOptionAttrSetScope(*AST, TU->src(), Pos, PM,
                                                Resolve)) {
              std::vector<std::string> Scope = Context->Scope;
              if (InFlakeOutputsBody)
                Scope = flake_schema::outputsBodyScope(Scope);
              if (OptionsReady)
                completion::completeOptionNames(
                    filterUsedOptionNames(
                        completeDerivedOptionsForFile(File, Scope, ""),
                        usedOptionNames(*Context->Attrs)),
                    ClientCaps.CompletionSnippets, List.items);
              if (!List.items.empty())
                return List;
            }
          }

          if (std::optional<OptionValueContext> Context =
                  findOptionValueContext(N, PM, Pos, Resolve)) {
            OptionValueContext ValueContext = *Context;
            if (InFlakeOutputsBody)
              ValueContext.Scope =
                  flake_schema::outputsBodyScope(ValueContext.Scope);
            if (OptionsReady)
              completion::completeOptionValue(
                  ValueContext,
                  resolveDerivedOptionInfosForFile(File, ValueContext.Scope),
                  ClientCaps.CompletionSnippets, nixpkgsClient(), TU->src(),
                  List.items);
            if (!List.items.empty())
              return List;
          }

          if (!UpExpr)
            return List;

          switch (UpExpr->kind()) {
          // In these cases, assume the cursor have "variable" scoping.
          case Node::NK_ExprVar: {
            completion::completeVarName(
                VLA, PM, static_cast<const nixf::ExprVar &>(*UpExpr),
                *nixpkgsClient(), List.items);
            return List;
          }
          // A "select" expression. e.g.
          // foo.a|
          // foo.|
          // foo.a.bar|
          case Node::NK_ExprSelect: {
            const auto &Select =
                static_cast<const nixf::ExprSelect &>(*UpExpr);
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

#include "nixd/Eval/AttrSetProvider.h"
#include "AttrSetValue.h"
#include "OptionDescription.h"
#include "nixd/Protocol/AttrSet.h"

#include "lspserver/Protocol.h"

#include <nix/cmd/common-eval-args.hh>
#include <nix/expr/attr-path.hh>
#include <nix/expr/nixexpr.hh>
#include <nix/store/store-open.hh>
#include <nix/util/position.hh>
#include <nixt/Value.h>

#include <optional>
#include <utility>

using namespace nixd;
using namespace lspserver;

namespace {

constexpr int MaxValueAttrNames = 64;

bool isModuleArgumentOptionPath(const AttrPathInfoParams &AttrPath) {
  if (AttrPath.size() < 2)
    return false;

  const auto Last = AttrPath.end() - 1;
  const auto BeforeLast = Last - 1;
  return *BeforeLast == "_module" &&
         (*Last == "args" || *Last == "specialArgs");
}

std::optional<Location> locationOfPos(const nix::Pos &Pos) {
  if (!Pos)
    return std::nullopt;

  std::optional<nix::SourcePath> Source = Pos.getSourcePath();
  if (!Source)
    return std::nullopt;

  Position LPos = {
      .line = static_cast<int64_t>(Pos.line - 1),
      .character = static_cast<int64_t>(Pos.column > 0 ? Pos.column - 1 : 0),
  };

  return Location{
      .uri = URIForFile::canonicalize(Source->path.abs(), Source->path.abs()),
      .range = {LPos, LPos},
  };
}

std::optional<Location> primaryErrorLocation(const nix::ErrorInfo &Info) {
  if (Info.pos)
    if (std::optional<Location> Location = locationOfPos(*Info.pos))
      return Location;

  for (const nix::Trace &Trace : Info.traces)
    if (Trace.pos)
      if (std::optional<Location> Location = locationOfPos(*Trace.pos))
        return Location;

  return std::nullopt;
}

EvalExprResponse evalExprError(const nix::BaseError &Err) {
  const nix::ErrorInfo &Info = Err.info();
  return EvalExprError{
      .Message = Info.msg.str(),
      .Location = primaryErrorLocation(Info),
  };
}

EvalExprResponse evalExprError(std::string Message) {
  return EvalExprError{.Message = std::move(Message)};
}

void fillOptionValueAttrNames(nix::EvalState &State, nix::Value &Option,
                              OptionDescription &R) {
  nix::Value *Value = nullptr;
  try {
    State.forceValue(Option, nix::noPos);
    if (Option.type() != nix::ValueType::nAttrs || !Option.attrs())
      return;
    const auto *It = Option.attrs()->get(State.symbols.create("value"));
    if (!It || !It->value)
      return;
    Value = It->value;
    State.forceValue(*Value, nix::noPos);
  } catch (const std::exception &) {
    return;
  }

  if (!Value || Value->type() != nix::ValueType::nAttrs || !Value->attrs())
    return;

  int Count = 0;
  for (const nix::Attr *Attr : Value->attrs()->lexicographicOrder(State.symbols)) {
    R.ValueAttrNames.emplace_back(State.symbols[Attr->name]);
    if (++Count >= MaxValueAttrNames)
      break;
  }
}

} // namespace

AttrSetProvider::AttrSetProvider(std::unique_ptr<InboundPort> In,
                                 std::unique_ptr<OutboundPort> Out)
    : LSPServer(std::move(In), std::move(Out)),
      State(new nix::EvalState({}, nix::openStore(), nix::fetchSettings,
                               nix::evalSettings)) {
  Registry.addMethod(rpcMethod::EvalExpr, this, &AttrSetProvider::onEvalExpr);
  Registry.addMethod(rpcMethod::AttrPathInfo, this,
                     &AttrSetProvider::onAttrPathInfo);
  Registry.addMethod(rpcMethod::AttrPathComplete, this,
                     &AttrSetProvider::onAttrPathComplete);
  Registry.addMethod(rpcMethod::OptionInfo, this,
                     &AttrSetProvider::onOptionInfo);
  Registry.addMethod(rpcMethod::OptionComplete, this,
                     &AttrSetProvider::onOptionComplete);
}

void AttrSetProvider::onEvalExpr(
    const EvalExprParams &Name,
    lspserver::Callback<EvalExprResponse> Reply) {
  try {
    nix::Expr *AST = state().parseExprFromString(Name, state().rootPath("."));
    state().eval(AST, Nixpkgs);
    Reply(std::nullopt);
    return;
  } catch (const nix::BaseError &Err) {
    Reply(evalExprError(Err));
    return;
  } catch (const std::exception &Err) {
    Reply(evalExprError(Err.what()));
    return;
  }
}

void AttrSetProvider::onAttrPathInfo(
    const AttrPathInfoParams &AttrPath,
    lspserver::Callback<AttrPathInfoResponse> Reply) {
  using RespT = AttrPathInfoResponse;
  Reply([&]() -> llvm::Expected<RespT> {
    try {
      if (AttrPath.empty())
        return error("attrpath is empty!");

      nix::Value &V = nixt::selectStrings(state(), Nixpkgs, AttrPath);
      state().forceValue(V, nix::noPos);
      return RespT{
          .Meta = metadataOf(state(), V),
          .PackageDesc = describePackage(state(), V),
          .ValueDesc = describeValue(state(), V),
      };
    } catch (const nix::BaseError &Err) {
      return error(Err.info().msg.str());
    } catch (const std::exception &Err) {
      return error(Err.what());
    }
  }());
}

void AttrSetProvider::onAttrPathComplete(
    const AttrPathCompleteParams &Params,
    lspserver::Callback<AttrPathCompleteResponse> Reply) {
  try {
    nix::Value &Scope = nixt::selectStrings(state(), Nixpkgs, Params.Scope);

    state().forceValue(Scope, nix::noPos);

    if (Scope.type() != nix::ValueType::nAttrs) {
      Reply(error("scope is not an attrset"));
      return;
    }

    return Reply(completeNames(Scope, state(), Params.Prefix));
  } catch (const nix::BaseError &Err) {
    return Reply(error(Err.info().msg.str()));
  } catch (const std::exception &Err) {
    return Reply(error(Err.what()));
  }
}

void AttrSetProvider::onOptionInfo(
    const AttrPathInfoParams &AttrPath,
    lspserver::Callback<OptionInfoResponse> Reply) {
  try {
    if (AttrPath.empty()) {
      Reply(error("attrpath is empty!"));
      return;
    }

    nix::Value Option = nixt::selectOptionInfo(
        state(), Nixpkgs, nixt::toSymbols(state().symbols, AttrPath));

    OptionInfoResponse R;
    fillOptionDescription(state(), Option, R);
    if (isModuleArgumentOptionPath(AttrPath))
      fillOptionValueAttrNames(state(), Option, R);

    Reply(std::move(R));
    return;
  } catch (const nix::BaseError &Err) {
    Reply(error(Err.info().msg.str()));
    return;
  } catch (const std::exception &Err) {
    Reply(error(Err.what()));
    return;
  }
}

void AttrSetProvider::onOptionComplete(
    const AttrPathCompleteParams &Params,
    lspserver::Callback<OptionCompleteResponse> Reply) {
  try {
    nix::Value Scope = nixt::selectOptions(
        state(), Nixpkgs, nixt::toSymbols(state().symbols, Params.Scope));

    state().forceValue(Scope, nix::noPos);

    if (Scope.type() != nix::ValueType::nAttrs) {
      Reply(error("scope is not an attrset"));
      return;
    }

    if (nixt::isOption(state(), Scope)) {
      Reply(error("scope is already an option"));
      return;
    }

    Reply(completeOptionsInScope(state(), Scope, Params.Prefix));
    return;
  } catch (const nix::BaseError &Err) {
    Reply(error(Err.info().msg.str()));
    return;
  } catch (const std::exception &Err) {
    Reply(error(Err.what()));
    return;
  }
}

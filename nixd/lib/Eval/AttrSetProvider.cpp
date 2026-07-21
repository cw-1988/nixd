#include "nixd/Eval/AttrSetProvider.h"
#include "AttrSetValue.h"
#include "OptionDescription.h"
#include "nixd/Protocol/AttrSet.h"

#include "lspserver/Protocol.h"

#include <nix/cmd/common-eval-args.hh>
#include <nix/expr/attr-path.hh>
#include <nix/expr/nixexpr.hh>
#include <nix/store/store-open.hh>
#include <nixt/Value.h>

using namespace nixd;
using namespace lspserver;

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
    const std::string &Name,
    lspserver::Callback<std::optional<std::string>> Reply) {
  try {
    nix::Expr *AST = state().parseExprFromString(Name, state().rootPath("."));
    state().eval(AST, Nixpkgs);
    Reply(std::nullopt);
    return;
  } catch (const nix::BaseError &Err) {
    Reply(error(Err.info().msg.str()));
    return;
  } catch (const std::exception &Err) {
    Reply(error(Err.what()));
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

    Reply(completeOptionsInScope(state(), Scope, Params.Prefix,
                                 Params.FullDescriptions));
    return;
  } catch (const nix::BaseError &Err) {
    Reply(error(Err.info().msg.str()));
    return;
  } catch (const std::exception &Err) {
    Reply(error(Err.what()));
    return;
  }
}

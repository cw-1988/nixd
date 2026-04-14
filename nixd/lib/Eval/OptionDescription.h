#pragma once

#include "nixd/Protocol/AttrSet.h"

#include <nix/expr/eval.hh>

#include <string_view>

namespace nixd {

void fillOptionDescription(nix::EvalState &State, nix::Value &V,
                           OptionDescription &R);

OptionCompleteResponse completeOptionsInScope(nix::EvalState &State,
                                              nix::Value &Scope,
                                              std::string_view Prefix);

} // namespace nixd

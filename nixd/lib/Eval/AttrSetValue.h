#pragma once

#include "nixd/Protocol/AttrSet.h"

#include <nix/expr/eval.hh>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nixd {

PackageDescription describePackage(nix::EvalState &State, nix::Value &Package);
ValueMeta metadataOf(nix::EvalState &State, nix::Value &V);
std::optional<ValueDescription> describeValue(nix::EvalState &State,
                                              nix::Value &V);
std::vector<std::string> completeNames(nix::Value &Scope,
                                       const nix::EvalState &State,
                                       std::string_view Prefix);

} // namespace nixd

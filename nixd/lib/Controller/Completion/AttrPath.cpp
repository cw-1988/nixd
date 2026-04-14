#include "Controller/Completion/Options.h"

#include "Controller/AST.h"

#include <optional>
#include <string>
#include <vector>

using namespace nixd;
using namespace nixf;

namespace nixd::completion::options::attr_path {

std::optional<AttrPathCompleteParams>
params(const Node &N, const ParentMapAnalysis &PM) {
  std::vector<std::string> Scope;
  using PathResult = FindAttrPathResult;
  auto R = findAttrPathForOptions(N, PM, Scope);
  if (R != PathResult::OK || Scope.empty())
    return std::nullopt;

  std::string Prefix = Scope.back();
  Scope.pop_back();
  return AttrPathCompleteParams{.Scope = std::move(Scope),
                                .Prefix = std::move(Prefix)};
}

} // namespace nixd::completion::options::attr_path

std::optional<AttrPathCompleteParams>
nixd::completion::optionAttrPathCompletionParams(
    const Node &N, const ParentMapAnalysis &PM) {
  return options::attr_path::params(N, PM);
}

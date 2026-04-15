#pragma once

#include "nixd/Controller/Option.h"

#include <string_view>
#include <vector>

namespace nixf {
class Node;
class ParentMapAnalysis;
} // namespace nixf

namespace nixd::flake_schema {

bool isFlakeFile(std::string_view File);
bool isInsideOutputsBody(const nixf::Node &Desc,
                         const nixf::ParentMapAnalysis &PM);
std::vector<std::string>
outputsBodyScope(const std::vector<std::string> &Scope);

std::vector<ResolvedOptionInfo>
resolve(const std::vector<std::string> &Scope);

std::vector<ResolvedOptionInfo>
resolveDerived(const std::vector<std::string> &Scope);

std::vector<ResolvedOptionField> complete(const std::vector<std::string> &Scope,
                                          const std::string &Prefix);

std::vector<ResolvedOptionField>
completeDerived(const std::vector<std::string> &Scope,
                const std::string &Prefix);

} // namespace nixd::flake_schema

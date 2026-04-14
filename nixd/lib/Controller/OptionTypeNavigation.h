#pragma once

#include "nixd/Controller/Option.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nixd::option_navigation {

enum class ChildKind {
  ListElement,
  AttrValue,
  FunctionBody,
};

struct ChildStep {
  ChildKind Kind;
  std::string Name;
};

std::string lowerTypeName(const OptionType &Type);
std::optional<OptionType> nestedType(const OptionType &Type,
                                     std::string_view Name);
std::optional<OptionType> elemTypeFor(const OptionType &Type,
                                      std::string_view LowerName);
std::optional<OptionType> nullOrTypeFor(const OptionType &Type);
std::optional<OptionType> functionResultTypeFor(const OptionType &Type);
std::vector<OptionType> alternativeTypesFor(const OptionType &Type,
                                            std::string_view LowerName);

bool isSubmoduleLike(const OptionType &Type);
bool hasSubOptionMetadata(const OptionType &Type);
bool isNonEmptyListType(const OptionType &Type);
bool hasFreeformCoverage(const OptionType &Type);
bool hasDynamicAttrCoverage(const OptionType &Type);

std::optional<OptionType>
deriveTypeForSuffix(const OptionType &Type,
                    const std::vector<std::string> &Suffix, size_t Index = 0);
std::optional<OptionType>
deriveTypeFromResolvedInfo(const ResolvedOptionInfo &Info,
                           const std::vector<std::string> &Suffix);

std::vector<OptionType> childTypesForStep(const OptionType &Type,
                                          const ChildStep &Step);
std::vector<OptionType>
descendValuePath(const OptionType &Type, const std::vector<ChildStep> &Steps);

} // namespace nixd::option_navigation

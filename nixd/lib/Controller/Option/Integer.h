#pragma once

#include "nixd/Controller/Option.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace nixd::option_integer {

bool classifyByName(std::string_view Name, ParsedOptionType &Parsed);
bool classifyByStableDescription(std::string_view Description,
                                 ParsedOptionType &Parsed);
void refineFromDescription(std::string_view Name, std::string_view Description,
                           ParsedOptionType &Parsed);
std::optional<std::int64_t> constantValue(const nixf::Expr &Value);
OptionValueMatch matchConstraint(const ParsedOptionType &Expected,
                                 const nixf::Expr &Value);

} // namespace nixd::option_integer

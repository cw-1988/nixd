#include "Integer.h"

#include "nixf/Basic/Nodes/Op.h"

#include <charconv>
#include <limits>

using namespace nixd;
using namespace nixf;

namespace {

void markInteger(ParsedOptionType &Parsed,
                 std::optional<std::int64_t> Min = std::nullopt,
                 std::optional<std::int64_t> Max = std::nullopt) {
  Parsed.Accepted.insert(OptionLiteralKind::Int);
  Parsed.Coverage = OptionTypeCoverage::Complete;
  if (Min || Max)
    Parsed.IntegerConstraint = OptionIntegerConstraint{Min, Max};
}

std::optional<std::int64_t> parseInteger(std::string_view S) {
  std::int64_t Value = 0;
  const char *Begin = S.data();
  const char *End = S.data() + S.size();
  const auto [Ptr, EC] = std::from_chars(Begin, End, Value);
  if (EC != std::errc() || Ptr != End)
    return std::nullopt;
  return Value;
}

std::optional<OptionIntegerConstraint>
parseIntBetweenDescription(std::string_view Description) {
  constexpr std::string_view Prefix = "integer between ";
  constexpr std::string_view Separator = " and ";
  constexpr std::string_view Suffix = " (both inclusive)";

  if (!Description.starts_with(Prefix) || !Description.ends_with(Suffix))
    return std::nullopt;

  Description.remove_prefix(Prefix.size());
  Description.remove_suffix(Suffix.size());

  const size_t SeparatorPos = Description.find(Separator);
  if (SeparatorPos == std::string_view::npos)
    return std::nullopt;

  const std::optional<std::int64_t> Min =
      parseInteger(Description.substr(0, SeparatorPos));
  const std::optional<std::int64_t> Max =
      parseInteger(Description.substr(SeparatorPos + Separator.size()));
  if (!Min || !Max)
    return std::nullopt;
  return OptionIntegerConstraint{Min, Max};
}

} // namespace

bool nixd::option_integer::classifyByName(std::string_view Name,
                                          ParsedOptionType &Parsed) {
  if (Name == "int" || Name == "integer" || Name == "signedint") {
    markInteger(Parsed);
    return true;
  }
  if (Name == "positiveint") {
    markInteger(Parsed, 1);
    return true;
  }
  if (Name == "unsignedint") {
    markInteger(Parsed, 0);
    return true;
  }
  if (Name == "unsignedint8") {
    markInteger(Parsed, 0, 255);
    return true;
  }
  if (Name == "unsignedint16" || Name == "port") {
    markInteger(Parsed, 0, 65535);
    return true;
  }
  if (Name == "unsignedint32") {
    markInteger(Parsed, 0, 4294967295LL);
    return true;
  }
  if (Name == "signedint8") {
    markInteger(Parsed, -128, 127);
    return true;
  }
  if (Name == "signedint16") {
    markInteger(Parsed, -32768, 32767);
    return true;
  }
  if (Name == "signedint32") {
    markInteger(Parsed, -2147483648LL, 2147483647);
    return true;
  }
  if (Name == "intbetween") {
    markInteger(Parsed);
    return true;
  }
  return false;
}

bool nixd::option_integer::classifyByStableDescription(
    std::string_view Description, ParsedOptionType &Parsed) {
  if (Description == "signed integer") {
    markInteger(Parsed);
    return true;
  }
  if (Description == "positive integer, meaning >0") {
    markInteger(Parsed, 1);
    return true;
  }
  if (Description == "unsigned integer, meaning >=0") {
    markInteger(Parsed, 0);
    return true;
  }
  if (Description ==
      "8 bit unsigned integer; between 0 and 255 (both inclusive)") {
    markInteger(Parsed, 0, 255);
    return true;
  }
  if (Description ==
      "16 bit unsigned integer; between 0 and 65535 (both inclusive)") {
    markInteger(Parsed, 0, 65535);
    return true;
  }
  if (Description ==
      "32 bit unsigned integer; between 0 and 4294967295 (both inclusive)") {
    markInteger(Parsed, 0, 4294967295LL);
    return true;
  }
  if (Description ==
      "8 bit signed integer; between -128 and 127 (both inclusive)") {
    markInteger(Parsed, -128, 127);
    return true;
  }
  if (Description ==
      "16 bit signed integer; between -32768 and 32767 (both inclusive)") {
    markInteger(Parsed, -32768, 32767);
    return true;
  }
  if (Description ==
      "32 bit signed integer; between -2147483648 and 2147483647 (both "
      "inclusive)") {
    markInteger(Parsed, -2147483648LL, 2147483647);
    return true;
  }
  if (const std::optional<OptionIntegerConstraint> Constraint =
          parseIntBetweenDescription(Description)) {
    markInteger(Parsed);
    Parsed.IntegerConstraint = Constraint;
    return true;
  }
  return false;
}

void nixd::option_integer::refineFromDescription(std::string_view Name,
                                                 std::string_view Description,
                                                 ParsedOptionType &Parsed) {
  if (Name != "intbetween")
    return;
  if (const std::optional<OptionIntegerConstraint> Constraint =
          parseIntBetweenDescription(Description))
    Parsed.IntegerConstraint = Constraint;
}

std::optional<std::int64_t>
nixd::option_integer::constantValue(const Expr &Value) {
  using NK = Node::NodeKind;
  switch (Value.kind()) {
  case NK::NK_ExprParen: {
    const auto &Paren = static_cast<const ExprParen &>(Value);
    if (const nixf::Expr *Inner = Paren.expr())
      return constantValue(*Inner);
    return std::nullopt;
  }
  case NK::NK_ExprInt:
    return static_cast<const ExprInt &>(Value).value();
  case NK::NK_ExprUnaryOp: {
    const auto &Unary = static_cast<const ExprUnaryOp &>(Value);
    if (!Unary.op().isNegate())
      return std::nullopt;
    const Expr *Inner = Unary.expr();
    if (!Inner)
      return std::nullopt;
    const std::optional<std::int64_t> InnerValue = constantValue(*Inner);
    if (!InnerValue || *InnerValue == std::numeric_limits<std::int64_t>::min())
      return std::nullopt;
    return -*InnerValue;
  }
  default:
    return std::nullopt;
  }
}

OptionValueMatch
nixd::option_integer::matchConstraint(const ParsedOptionType &Expected,
                                      const Expr &Value) {
  if (!Expected.IntegerConstraint)
    return OptionValueMatch::Matches;

  const std::optional<std::int64_t> ActualValue = constantValue(Value);
  if (!ActualValue)
    return OptionValueMatch::Unknown;
  if (Expected.IntegerConstraint->Min &&
      *ActualValue < *Expected.IntegerConstraint->Min)
    return OptionValueMatch::Mismatches;
  if (Expected.IntegerConstraint->Max &&
      *ActualValue > *Expected.IntegerConstraint->Max)
    return OptionValueMatch::Mismatches;
  return OptionValueMatch::Matches;
}

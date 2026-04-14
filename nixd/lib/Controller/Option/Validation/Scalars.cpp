#include "Support.h"

#include "Controller/Option/DiagnosticsSupport.h"
#include "Controller/Option/Integer.h"

#include <cctype>
#include <optional>
#include <string>
#include <string_view>

using namespace nixd;
using namespace nixd::option_diagnostics;
using namespace nixf;

namespace nixd::option_diagnostics::validation {
namespace {

bool enumValueMatches(const OptionType::EnumValue &Expected,
                      const Expr &Value) {
  if (Expected.IsNull)
    return literalNull(Value);
  if (Expected.String) {
    const std::optional<std::string> Actual = literalString(Value);
    return Actual && *Actual == *Expected.String;
  }
  if (Expected.Integer) {
    const std::optional<std::int64_t> Actual =
        option_integer::constantValue(Value);
    return Actual && *Actual == *Expected.Integer;
  }
  if (Expected.Boolean) {
    const std::optional<bool> Actual = literalBool(Value);
    return Actual && *Actual == *Expected.Boolean;
  }
  return false;
}

bool isRegexMetachar(char C) {
  switch (C) {
  case '.':
  case '^':
  case '$':
  case '|':
  case '(':
  case ')':
  case '[':
  case ']':
  case '{':
  case '}':
  case '*':
  case '+':
  case '?':
  case '\\':
    return true;
  default:
    return false;
  }
}

std::optional<std::string>
conservativeLiteralPattern(std::string_view Pattern) {
  std::string Literal;
  Literal.reserve(Pattern.size());

  for (size_t I = 0; I < Pattern.size(); ++I) {
    char C = Pattern[I];
    if (C == '\\') {
      if (++I >= Pattern.size())
        return std::nullopt;
      char Escaped = Pattern[I];
      if (!isRegexMetachar(Escaped))
        return std::nullopt;
      Literal.push_back(Escaped);
      continue;
    }

    if (isRegexMetachar(C))
      return std::nullopt;
    Literal.push_back(C);
  }

  return Literal;
}

std::optional<bool> conservativePatternMatches(std::string_view Pattern,
                                               std::string_view Literal) {
  std::optional<std::string> Expected = conservativeLiteralPattern(Pattern);
  if (!Expected)
    return std::nullopt;
  return *Expected == Literal;
}

} // namespace

SchemaMatch scalarKindMatch(const OptionType &Type, const Expr &Value,
                            OptionLiteralKind Actual) {
  if (Type.EnumValues.empty())
    return SchemaMatch::Unknown;
  if (Actual == OptionLiteralKind::Unknown)
    return SchemaMatch::Unknown;
  for (const OptionType::EnumValue &Enum : Type.EnumValues) {
    if (enumValueMatches(Enum, Value))
      return SchemaMatch::Matches;
  }
  return SchemaMatch::Mismatches;
}

SchemaMatch stringConstraintMatch(const OptionType &Type, const Expr &Value,
                                  OptionLiteralKind Actual) {
  if (!Type.String)
    return SchemaMatch::Unknown;
  if (Actual != OptionLiteralKind::String)
    return Actual == OptionLiteralKind::Unknown ? SchemaMatch::Unknown
                                                : SchemaMatch::Mismatches;

  const std::optional<std::string> Literal = literalString(Value);
  if (!Literal)
    return SchemaMatch::Unknown;

  if (Type.String->NonEmpty && Literal->empty())
    return SchemaMatch::Mismatches;
  if (Type.String->SingleLine && Literal->find('\n') != std::string::npos)
    return SchemaMatch::Mismatches;
  if (Type.String->PasswdEntry && (Literal->find('\n') != std::string::npos ||
                                   Literal->find(':') != std::string::npos))
    return SchemaMatch::Mismatches;
  if (Type.String->SystemdUnitName) {
    auto HasSuffix = [](std::string_view S) {
      for (std::string_view Suffix :
           {".automount", ".device", ".mount", ".path", ".scope", ".service",
            ".slice", ".socket", ".swap", ".target", ".timer"}) {
        if (S.ends_with(Suffix) && S.size() > Suffix.size())
          return true;
      }
      return false;
    };

    if (!HasSuffix(*Literal))
      return SchemaMatch::Mismatches;
    for (char C : *Literal) {
      unsigned char UC = static_cast<unsigned char>(C);
      if (C == '/' || std::isspace(UC) != 0)
        return SchemaMatch::Mismatches;
    }
  }
  if (Type.String->Pattern) {
    std::optional<bool> Matches =
        conservativePatternMatches(*Type.String->Pattern, *Literal);
    if (!Matches)
      return SchemaMatch::Unknown;
    if (!*Matches)
      return SchemaMatch::Mismatches;
  }
  return SchemaMatch::Matches;
}

} // namespace nixd::option_diagnostics::validation

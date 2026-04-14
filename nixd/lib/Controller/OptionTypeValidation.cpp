#include "OptionTypeValidation.h"

#include "OptionDiagnosticsSupport.h"
#include "OptionInteger.h"
#include "OptionTypeNavigation.h"

#include <nixf/Basic/Nodes/Attrs.h>
#include <nixf/Basic/Nodes/Lambda.h>
#include <nixf/Basic/Nodes/Op.h>
#include <nixf/Basic/Nodes/Simple.h>

#include <algorithm>
#include <cctype>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace nixd;
using namespace nixd::option_diagnostics;
using namespace nixf;

namespace {

std::optional<OptionType> nestedType(const OptionType &Type,
                                     std::string_view Name) {
  auto It = Type.NestedTypes.find(std::string(Name));
  if (It == Type.NestedTypes.end())
    return std::nullopt;
  return It->second;
}

std::string_view trimOuterParens(std::string_view S) {
  if (S.size() < 2 || S.front() != '(' || S.back() != ')')
    return S;
  return S.substr(1, S.size() - 2);
}

std::optional<OptionType> elemTypeFor(const OptionType &Type,
                                      std::string_view LowerName) {
  return option_navigation::elemTypeFor(Type, LowerName);
}

std::optional<OptionType> nullOrTypeFor(const OptionType &Type) {
  return option_navigation::nullOrTypeFor(Type);
}

std::vector<OptionType> alternativeTypesFor(const OptionType &Type,
                                            std::string_view LowerName) {
  return option_navigation::alternativeTypesFor(Type, LowerName);
}

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

std::optional<bool> conservativePatternMatches(std::string_view Pattern,
                                               std::string_view Literal);

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

bool isStorePath(std::string_view S) { return S.starts_with("/nix/store/"); }

std::optional<OptionType::PathConstraint>
pathConstraintFor(const OptionType &Type) {
  if (Type.Path)
    return Type.Path;
  if (!Type.Description)
    return std::nullopt;

  std::string LowerName = Type.Name ? toLowerCopy(*Type.Name) : "";
  if (!LowerName.empty() && LowerName != "path")
    return std::nullopt;

  std::string Lower = toLowerCopy(*Type.Description);
  std::string_view View = trimOuterParens(Lower);
  if (View != "absolute path")
    return std::nullopt;

  OptionType::PathConstraint Constraint;
  Constraint.Absolute = true;
  Constraint.AcceptsStringLike = true;
  return Constraint;
}

SchemaMatch pathConstraintMatch(const OptionType &Type, const Expr &Value,
                                OptionLiteralKind Actual) {
  std::optional<OptionType::PathConstraint> Constraint =
      pathConstraintFor(Type);
  if (!Constraint)
    return SchemaMatch::Unknown;

  std::optional<std::string> Text;
  if (Actual == OptionLiteralKind::Path)
    Text = pathLiteralText(Value);
  else if (Actual == OptionLiteralKind::String && Constraint->AcceptsStringLike)
    Text = literalString(Value);
  else if (Actual == OptionLiteralKind::Unknown)
    return SchemaMatch::Unknown;
  else
    return SchemaMatch::Mismatches;

  if (!Text)
    return SchemaMatch::Unknown;
  if (Constraint->Absolute && !Text->starts_with("/"))
    return SchemaMatch::Mismatches;
  if (Constraint->InStore && !isStorePath(*Text))
    return SchemaMatch::Mismatches;
  return SchemaMatch::Matches;
}

bool isDerivationLikeAttrset(const ExprAttrs &Attrs) {
  const auto &Static = Attrs.sema().staticAttrs();
  if (Static.contains("outPath"))
    return true;
  auto It = Static.find("type");
  if (It == Static.end() || !It->second.value())
    return false;
  const std::optional<std::string> TypeValue =
      literalString(*It->second.value());
  return TypeValue && *TypeValue == "derivation";
}

SchemaMatch packageMatch(const Expr &Value, OptionLiteralKind Actual) {
  const Expr &Stripped = stripParens(Value);
  if (Actual == OptionLiteralKind::Path)
    return SchemaMatch::Matches;
  if (Actual == OptionLiteralKind::String) {
    const std::optional<std::string> Literal = literalString(Stripped);
    if (!Literal)
      return SchemaMatch::Unknown;
    return isStorePath(*Literal) ? SchemaMatch::Matches
                                 : SchemaMatch::Mismatches;
  }
  if (Actual == OptionLiteralKind::AttrSet &&
      Stripped.kind() == Node::NK_ExprAttrs)
    return isDerivationLikeAttrset(static_cast<const ExprAttrs &>(Stripped))
               ? SchemaMatch::Matches
               : SchemaMatch::Unknown;
  if (Actual == OptionLiteralKind::Unknown)
    return SchemaMatch::Unknown;
  return SchemaMatch::Mismatches;
}

bool sameRange(const LexerCursorRange &LHS, const LexerCursorRange &RHS) {
  return LHS.lCur().offset() == RHS.lCur().offset() &&
         LHS.rCur().offset() == RHS.rCur().offset();
}

ValidationResult mismatchResult(const Expr &Value,
                                const std::vector<std::string> &Scope,
                                OptionLiteralKind Actual,
                                const OptionType &Type) {
  ValidationResult Result;
  Result.Match = SchemaMatch::Mismatches;
  Result.Diagnostics.emplace_back(
      makeTypeDiagnostic(Value, Scope, Actual, Type));
  return Result;
}

ValidationResult unknownResult() { return ValidationResult{}; }

ValidationResult matchesResult() {
  ValidationResult Result;
  Result.Match = SchemaMatch::Matches;
  return Result;
}

ValidationResult
validateAlternatives(const std::vector<OptionType> &Alternatives,
                     const Expr &Value, const std::vector<std::string> &Scope,
                     const ParentMapAnalysis &PM,
                     const VariableLookupAnalysis *VLA) {
  ValidationResult FirstMismatch;
  bool SawMismatch = false;
  for (const OptionType &Alternative : Alternatives) {
    ValidationResult Result = validateType(Alternative, Value, Scope, PM, VLA);
    if (Result.Match == SchemaMatch::Matches)
      return matchesResult();
    if (Result.Match == SchemaMatch::Mismatches && !SawMismatch) {
      FirstMismatch = std::move(Result);
      SawMismatch = true;
    }
  }
  return SawMismatch ? std::move(FirstMismatch) : unknownResult();
}

ValidationResult validateListOf(const OptionType &Type, const OptionType &Elem,
                                const Expr &Value,
                                const std::vector<std::string> &Scope,
                                const ParentMapAnalysis &PM,
                                const VariableLookupAnalysis *VLA,
                                OptionLiteralKind Actual) {
  if (Actual != OptionLiteralKind::List)
    return Actual == OptionLiteralKind::Unknown
               ? unknownResult()
               : mismatchResult(Value, Scope, Actual, Type);

  const Expr &Stripped = stripParens(Value);
  if (Stripped.kind() != Node::NK_ExprList)
    return matchesResult();

  ValidationResult Result = matchesResult();
  const auto &List = static_cast<const ExprList &>(Stripped);
  if (List.elements().empty() && option_navigation::isNonEmptyListType(Type))
    return mismatchResult(Value, Scope, Actual, Type);

  for (const auto &Element : List.elements()) {
    if (!Element)
      continue;
    ValidationResult Child =
        validateType(Elem, *Element, appendScope(Scope, "[]"), PM, VLA);
    if (Child.Match == SchemaMatch::Mismatches) {
      Result.Match = SchemaMatch::Mismatches;
      std::move(Child.Diagnostics.begin(), Child.Diagnostics.end(),
                std::back_inserter(Result.Diagnostics));
    }
  }
  return Result;
}

ValidationResult validateAttrsOf(const OptionType &Type, const OptionType &Elem,
                                 const Expr &Value,
                                 const std::vector<std::string> &Scope,
                                 const ParentMapAnalysis &PM,
                                 const VariableLookupAnalysis *VLA,
                                 OptionLiteralKind Actual);

ValidationResult validateLoaOf(const OptionType &Type, const OptionType &Elem,
                               const Expr &Value,
                               const std::vector<std::string> &Scope,
                               const ParentMapAnalysis &PM,
                               const VariableLookupAnalysis *VLA,
                               OptionLiteralKind Actual) {
  if (Actual != OptionLiteralKind::List)
    return Actual == OptionLiteralKind::Unknown
               ? unknownResult()
               : mismatchResult(Value, Scope, Actual, Type);

  const Expr &Stripped = stripParens(Value);
  if (Stripped.kind() != Node::NK_ExprList)
    return matchesResult();

  ValidationResult Result = matchesResult();
  const auto &List = static_cast<const ExprList &>(Stripped);
  if (List.elements().empty() && option_navigation::isNonEmptyListType(Type))
    return mismatchResult(Value, Scope, Actual, Type);

  for (const auto &Element : List.elements()) {
    if (!Element)
      continue;
    ValidationResult Child = validateAttrsOf(
        Type, Elem, *Element, appendScope(Scope, "[]"), PM, VLA,
        classifyOptionLiteral(resolveStaticValue(*Element, PM, VLA)));
    if (Child.Match == SchemaMatch::Mismatches) {
      Result.Match = SchemaMatch::Mismatches;
      std::move(Child.Diagnostics.begin(), Child.Diagnostics.end(),
                std::back_inserter(Result.Diagnostics));
    }
  }
  return Result;
}

ValidationResult validateAttrsOf(const OptionType &Type, const OptionType &Elem,
                                 const Expr &Value,
                                 const std::vector<std::string> &Scope,
                                 const ParentMapAnalysis &PM,
                                 const VariableLookupAnalysis *VLA,
                                 OptionLiteralKind Actual) {
  if (Actual != OptionLiteralKind::AttrSet)
    return Actual == OptionLiteralKind::Unknown
               ? unknownResult()
               : mismatchResult(Value, Scope, Actual, Type);

  const Expr &Stripped = stripParens(Value);
  if (Stripped.kind() != Node::NK_ExprAttrs)
    return matchesResult();

  ValidationResult Result = matchesResult();
  const auto &Attrs = static_cast<const ExprAttrs &>(Stripped);
  for (const auto &[Name, Attr] : Attrs.sema().staticAttrs()) {
    if (!Attr.value())
      continue;
    ValidationResult Child =
        validateType(Elem, *Attr.value(), appendScope(Scope, Name), PM, VLA);
    if (Child.Match == SchemaMatch::Mismatches) {
      Result.Match = SchemaMatch::Mismatches;
      std::move(Child.Diagnostics.begin(), Child.Diagnostics.end(),
                std::back_inserter(Result.Diagnostics));
    }
  }
  return Result;
}

void validateRequiredSubOptions(const OptionType &Type, const ExprAttrs &Attrs,
                                const Expr &OriginalValue,
                                const std::vector<std::string> &Scope,
                                ValidationResult &Result) {
  if (!Type.KnownSubOptionsComplete)
    return;
  if (Attrs.sema().isRecursive() || !Attrs.sema().dynamicAttrs().empty() ||
      hasInheritBinding(Attrs) ||
      Attrs.sema().staticAttrs().contains("imports"))
    return;

  for (const auto &[Name, Summary] : Type.KnownSubOptions) {
    if (!Summary.Required || Attrs.sema().staticAttrs().contains(Name))
      continue;
    Result.Match = SchemaMatch::Mismatches;
    Result.Diagnostics.emplace_back(
        makeRequiredDiagnostic(OriginalValue, appendScope(Scope, Name)));
  }
}

ValidationResult validateSubmoduleAttrset(const OptionType &Type,
                                          const ExprAttrs &Attrs,
                                          const Expr &OriginalValue,
                                          const std::vector<std::string> &Scope,
                                          const ParentMapAnalysis &PM,
                                          const VariableLookupAnalysis *VLA) {
  ValidationResult Result = matchesResult();
  std::optional<OptionType> Freeform = nestedType(Type, "freeformType");
  const bool CanProveUnknownSubOptions =
      Type.KnownSubOptionsComplete &&
      (!Type.NestedTypes.empty() || !Type.KnownSubOptions.empty()) &&
      !Attrs.sema().isRecursive() && Attrs.sema().dynamicAttrs().empty() &&
      !hasInheritBinding(Attrs) &&
      !Attrs.sema().staticAttrs().contains("imports");

  for (const auto &[Name, Attr] : Attrs.sema().staticAttrs()) {
    if (!Attr.value())
      continue;
    if (Name == "imports" || Name == "options" || Name == "disabledModules" ||
        Name == "_module" || Name == "meta")
      continue;

    if (Name == "config" &&
        stripParens(*Attr.value()).kind() == Node::NK_ExprAttrs) {
      ValidationResult Config = validateSubmoduleAttrset(
          Type, static_cast<const ExprAttrs &>(stripParens(*Attr.value())),
          *Attr.value(), Scope, PM, VLA);
      if (Config.Match == SchemaMatch::Mismatches) {
        Result.Match = SchemaMatch::Mismatches;
        std::move(Config.Diagnostics.begin(), Config.Diagnostics.end(),
                  std::back_inserter(Result.Diagnostics));
      }
      continue;
    }

    auto Known = Type.NestedTypes.find(Name);
    if (Known != Type.NestedTypes.end()) {
      ValidationResult Child = validateType(Known->second, *Attr.value(),
                                            appendScope(Scope, Name), PM, VLA);
      if (Child.Match == SchemaMatch::Mismatches) {
        Result.Match = SchemaMatch::Mismatches;
        std::move(Child.Diagnostics.begin(), Child.Diagnostics.end(),
                  std::back_inserter(Result.Diagnostics));
      }
      continue;
    }

    if (Type.KnownSubOptions.contains(Name))
      continue;

    if (Freeform) {
      ValidationResult Child = validateType(*Freeform, *Attr.value(),
                                            appendScope(Scope, Name), PM, VLA);
      if (Child.Match == SchemaMatch::Mismatches) {
        Result.Match = SchemaMatch::Mismatches;
        std::move(Child.Diagnostics.begin(), Child.Diagnostics.end(),
                  std::back_inserter(Result.Diagnostics));
      }
      continue;
    }

    if (CanProveUnknownSubOptions) {
      Result.Match = SchemaMatch::Mismatches;
      Result.Diagnostics.emplace_back(
          makeUnknownDiagnostic(Attr.key(), appendScope(Scope, Name)));
    }
  }

  validateRequiredSubOptions(Type, Attrs, OriginalValue, Scope, Result);
  return Result;
}

ValidationResult validateSubmodule(const OptionType &Type, const Expr &Value,
                                   const std::vector<std::string> &Scope,
                                   const ParentMapAnalysis &PM,
                                   const VariableLookupAnalysis *VLA,
                                   OptionLiteralKind Actual) {
  if (Actual != OptionLiteralKind::AttrSet &&
      Actual != OptionLiteralKind::Function &&
      Actual != OptionLiteralKind::Path)
    return Actual == OptionLiteralKind::Unknown
               ? unknownResult()
               : mismatchResult(Value, Scope, Actual, Type);

  const Expr &Stripped = stripParens(Value);
  if (Stripped.kind() == Node::NK_ExprAttrs)
    return validateSubmoduleAttrset(
        Type, static_cast<const ExprAttrs &>(Stripped), Value, Scope, PM, VLA);

  if (Stripped.kind() == Node::NK_ExprLambda) {
    const Expr *Body = static_cast<const ExprLambda &>(Stripped).body();
    if (Body && stripParens(*Body).kind() == Node::NK_ExprAttrs)
      return validateSubmoduleAttrset(
          Type, static_cast<const ExprAttrs &>(stripParens(*Body)), *Body,
          Scope, PM, VLA);
  }

  return matchesResult();
}

bool hasSubOptionMetadata(const OptionType &Type) {
  return option_navigation::hasSubOptionMetadata(Type);
}

ValidationResult validateSubOptionNamespace(
    const OptionType &Type, const Expr &Value,
    const std::vector<std::string> &Scope, const ParentMapAnalysis &PM,
    const VariableLookupAnalysis *VLA, OptionLiteralKind Actual) {
  if (Actual != OptionLiteralKind::AttrSet)
    return unknownResult();

  const Expr &Stripped = stripParens(Value);
  if (Stripped.kind() != Node::NK_ExprAttrs)
    return matchesResult();

  return validateSubmoduleAttrset(
      Type, static_cast<const ExprAttrs &>(Stripped), Value, Scope, PM, VLA);
}

ValidationResult validateFunctionTo(
    const OptionType &Type, const OptionType &ResultType, const Expr &Value,
    const std::vector<std::string> &Scope, const ParentMapAnalysis &PM,
    const VariableLookupAnalysis *VLA, OptionLiteralKind Actual) {
  if (Actual != OptionLiteralKind::Function)
    return Actual == OptionLiteralKind::Unknown
               ? unknownResult()
               : mismatchResult(Value, Scope, Actual, Type);

  const Expr &Stripped = stripParens(Value);
  if (Stripped.kind() != Node::NK_ExprLambda)
    return matchesResult();

  const Expr *Body = static_cast<const ExprLambda &>(Stripped).body();
  if (!Body)
    return matchesResult();

  ValidationResult BodyResult =
      validateType(ResultType, *Body, appendScope(Scope, "<return>"), PM, VLA);
  if (BodyResult.Match == SchemaMatch::Unknown)
    return matchesResult();
  return BodyResult;
}

} // namespace

ValidationResult
option_diagnostics::validateType(const OptionType &Type, const Expr &Value,
                                 const std::vector<std::string> &Scope,
                                 const ParentMapAnalysis &PM,
                                 const VariableLookupAnalysis *VLA) {
  const Expr &Resolved = resolveStaticValue(Value, PM, VLA);
  const OptionLiteralKind Actual = classifyOptionLiteral(Resolved);
  const std::string LowerName = Type.Name ? toLowerCopy(*Type.Name) : "";

  if (LowerName == "package") {
    SchemaMatch Match = packageMatch(Resolved, Actual);
    if (Match == SchemaMatch::Matches)
      return matchesResult();
    if (Match == SchemaMatch::Mismatches)
      return mismatchResult(Value, Scope, Actual, Type);
    return unknownResult();
  }

  if (!Type.EnumValues.empty()) {
    SchemaMatch Match = scalarKindMatch(Type, Resolved, Actual);
    if (Match == SchemaMatch::Matches)
      return matchesResult();
    if (Match == SchemaMatch::Mismatches)
      return mismatchResult(Value, Scope, Actual, Type);
    return unknownResult();
  }

  if (Type.String) {
    SchemaMatch Match = stringConstraintMatch(Type, Resolved, Actual);
    if (Match == SchemaMatch::Matches)
      return matchesResult();
    if (Match == SchemaMatch::Mismatches)
      return mismatchResult(Value, Scope, Actual, Type);
    return unknownResult();
  }

  if (pathConstraintFor(Type)) {
    SchemaMatch Match = pathConstraintMatch(Type, Resolved, Actual);
    if (Match == SchemaMatch::Matches)
      return matchesResult();
    if (Match == SchemaMatch::Mismatches)
      return mismatchResult(Value, Scope, Actual, Type);
    return unknownResult();
  }

  if (LowerName == "nullor") {
    if (Actual == OptionLiteralKind::Null)
      return matchesResult();
    if (std::optional<OptionType> Elem = nullOrTypeFor(Type)) {
      ValidationResult Result = validateType(*Elem, Value, Scope, PM, VLA);
      if (Result.Match == SchemaMatch::Mismatches &&
          Result.Diagnostics.size() == 1 &&
          sameRange(Result.Diagnostics.front().Range, Value.range()))
        return mismatchResult(Value, Scope, Actual, Type);
      return Result;
    }
  }

  if (LowerName == "unique") {
    if (std::optional<OptionType> Elem = elemTypeFor(Type, LowerName))
      return validateType(*Elem, Value, Scope, PM, VLA);
  }

  std::vector<OptionType> Alternatives = alternativeTypesFor(Type, LowerName);
  if (!Alternatives.empty())
    return validateAlternatives(Alternatives, Value, Scope, PM, VLA);

  if (LowerName == "listof") {
    if (std::optional<OptionType> Elem = elemTypeFor(Type, LowerName))
      return validateListOf(Type, *Elem, Value, Scope, PM, VLA, Actual);
  }

  if (LowerName == "loaof") {
    if (std::optional<OptionType> Elem = elemTypeFor(Type, LowerName))
      return validateLoaOf(Type, *Elem, Value, Scope, PM, VLA, Actual);
  }

  if (LowerName == "attrsof" || LowerName == "lazyattrsof" ||
      LowerName == "attrswith") {
    if (std::optional<OptionType> Elem = elemTypeFor(Type, LowerName))
      return validateAttrsOf(Type, *Elem, Value, Scope, PM, VLA, Actual);
  }

  if (LowerName == "submodule" || LowerName == "submodulewith")
    return validateSubmodule(Type, Value, Scope, PM, VLA, Actual);

  if (hasSubOptionMetadata(Type))
    return validateSubOptionNamespace(Type, Value, Scope, PM, VLA, Actual);

  if (LowerName == "functionto") {
    if (std::optional<OptionType> Result = nestedType(Type, "resultType"))
      return validateFunctionTo(Type, *Result, Value, Scope, PM, VLA, Actual);
    if (std::optional<OptionType> Result = nestedType(Type, "elemType"))
      return validateFunctionTo(Type, *Result, Value, Scope, PM, VLA, Actual);
  }

  const std::optional<ParsedOptionType> Expected = parseOptionType(Type);
  if (!Expected)
    return unknownResult();
  const OptionValueMatch Match = optionValueMatch(*Expected, Resolved, Actual);
  if (Match == OptionValueMatch::Matches)
    return matchesResult();
  if (Match == OptionValueMatch::Mismatches)
    return mismatchResult(Value, Scope, Actual, Type);
  return unknownResult();
}

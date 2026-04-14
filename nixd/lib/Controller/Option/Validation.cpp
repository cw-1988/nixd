#include "Validation.h"

#include "DiagnosticsSupport.h"
#include "Navigation.h"
#include "Validation/Support.h"

#include <nixf/Basic/Nodes/Attrs.h>
#include <nixf/Basic/Nodes/Lambda.h>
#include <nixf/Basic/Nodes/Op.h>
#include <nixf/Basic/Nodes/Simple.h>

#include <algorithm>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace nixd;
using namespace nixd::option_diagnostics;
using namespace nixd::option_diagnostics::validation;
using namespace nixf;

namespace {

std::optional<OptionType> nestedType(const OptionType &Type,
                                     std::string_view Name) {
  auto It = Type.NestedTypes.find(std::string(Name));
  if (It == Type.NestedTypes.end())
    return std::nullopt;
  return It->second;
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

  if (hasPathConstraint(Type)) {
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

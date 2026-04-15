#include "Support.h"

#include "Controller/Option/DiagnosticsSupport.h"

#include <optional>
#include <string>
#include <string_view>

using namespace nixd;
using namespace nixd::option_diagnostics;
using namespace nixf;

namespace nixd::option_diagnostics::validation {
namespace {

std::string_view trimOuterParens(std::string_view S) {
  if (S.size() < 2 || S.front() != '(' || S.back() != ')')
    return S;
  return S.substr(1, S.size() - 2);
}

bool isStorePath(std::string_view S) { return S.starts_with("/nix/store/"); }

std::optional<bool> isAbsolutePathValue(const Expr &Value,
                                        OptionLiteralKind Actual) {
  if (Actual == OptionLiteralKind::Path)
    return true;
  if (Actual != OptionLiteralKind::String)
    return std::nullopt;

  std::optional<std::string> Text = literalString(Value);
  if (!Text)
    return std::nullopt;
  return Text->starts_with("/");
}

std::optional<bool> isStorePathValue(const Expr &Value,
                                     OptionLiteralKind Actual) {
  if (Actual == OptionLiteralKind::String) {
    std::optional<std::string> Text = literalString(Value);
    if (!Text)
      return std::nullopt;
    return isStorePath(*Text);
  }

  if (Actual != OptionLiteralKind::Path)
    return std::nullopt;

  std::optional<std::string> Text = pathLiteralText(Value);
  if (!Text)
    return std::nullopt;

  // Relative path literals evaluate to absolute source paths, not store paths.
  // Search paths depend on the evaluator's lookup path, so leave them unknown.
  if (Text->starts_with("<"))
    return std::nullopt;
  return isStorePath(*Text);
}

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

} // namespace

bool hasPathConstraint(const OptionType &Type) {
  return pathConstraintFor(Type).has_value();
}

SchemaMatch pathConstraintMatch(const OptionType &Type, const Expr &Value,
                                OptionLiteralKind Actual) {
  std::optional<OptionType::PathConstraint> Constraint =
      pathConstraintFor(Type);
  if (!Constraint)
    return SchemaMatch::Unknown;

  if (Actual == OptionLiteralKind::Unknown)
    return SchemaMatch::Unknown;

  const bool AcceptsPathLiteral =
      Actual == OptionLiteralKind::Path &&
      (!Constraint->InStore || *Constraint->InStore);
  const bool AcceptsString =
      Actual == OptionLiteralKind::String && Constraint->AcceptsStringLike;
  if (!AcceptsPathLiteral && !AcceptsString)
    return SchemaMatch::Mismatches;

  if (Constraint->Absolute) {
    std::optional<bool> ActualAbsolute = isAbsolutePathValue(Value, Actual);
    if (!ActualAbsolute)
      return SchemaMatch::Unknown;
    if (*ActualAbsolute != *Constraint->Absolute)
      return SchemaMatch::Mismatches;
  }

  if (Constraint->InStore) {
    std::optional<bool> ActualInStore = isStorePathValue(Value, Actual);
    if (!ActualInStore)
      return SchemaMatch::Unknown;
    if (*ActualInStore != *Constraint->InStore)
      return SchemaMatch::Mismatches;
  }

  return SchemaMatch::Matches;
}

} // namespace nixd::option_diagnostics::validation

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

} // namespace nixd::option_diagnostics::validation

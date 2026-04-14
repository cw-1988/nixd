#include "Support.h"

#include "Controller/Option/DiagnosticsSupport.h"

#include <optional>
#include <string>
#include <string_view>

#include <nixf/Basic/Nodes/Attrs.h>

using namespace nixd;
using namespace nixd::option_diagnostics;
using namespace nixf;

namespace nixd::option_diagnostics::validation {
namespace {

bool isStorePath(std::string_view S) { return S.starts_with("/nix/store/"); }

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

} // namespace

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

} // namespace nixd::option_diagnostics::validation

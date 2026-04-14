#include "OptionDiagnosticsSupport.h"

#include <nixf/Basic/Nodes/Lambda.h>
#include <nixf/Basic/Nodes/Simple.h>

#include <cctype>
#include <sstream>
#include <utility>

using namespace nixd;
using namespace nixf;

namespace {

constexpr unsigned MaxConstResolveDepth = 4;

std::string renderOptionType(const OptionType &Type) {
  std::ostringstream OS;
  if (Type.Name)
    OS << *Type.Name;
  if (Type.Description) {
    if (OS.tellp() > 0)
      OS << " ";
    OS << *Type.Description;
  }
  return OS.str();
}

} // namespace

std::string option_diagnostics::toLowerCopy(std::string_view S) {
  std::string Lower;
  Lower.reserve(S.size());
  for (char C : S)
    Lower.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(C))));
  return Lower;
}

std::string
option_diagnostics::renderScope(const std::vector<std::string> &Scope) {
  std::ostringstream OS;
  for (size_t I = 0; I < Scope.size(); ++I) {
    if (Scope[I] == "[]") {
      OS << "[]";
      continue;
    }
    if (I)
      OS << ".";
    OS << Scope[I];
  }
  return OS.str();
}

std::string option_diagnostics::renderExpected(const OptionType &Type) {
  std::string Rendered = renderOptionType(Type);
  return Rendered.empty() ? "unknown" : Rendered;
}

std::vector<std::string>
option_diagnostics::appendScope(std::vector<std::string> Scope,
                                std::string Name) {
  Scope.emplace_back(std::move(Name));
  return Scope;
}

const Expr &option_diagnostics::stripParens(const Expr &Value) {
  if (Value.kind() != Node::NK_ExprParen)
    return Value;
  const auto &Paren = static_cast<const ExprParen &>(Value);
  if (!Paren.expr())
    return Value;
  return stripParens(*Paren.expr());
}

const Expr &
option_diagnostics::resolveStaticValue(const Expr &Value,
                                       const ParentMapAnalysis &PM,
                                       const VariableLookupAnalysis *VLA) {
  auto Resolve = [&](const Expr &Current, unsigned Depth,
                     const auto &Self) -> const Expr & {
    const Expr &Stripped = stripParens(Current);
    if (!Depth || !VLA || Stripped.kind() != Node::NK_ExprVar)
      return Stripped;

    const auto &Var = static_cast<const ExprVar &>(Stripped);
    const std::string &Name = Var.id().name();
    if (Name == "null" || Name == "true" || Name == "false")
      return Stripped;

    using ResultKind = VariableLookupAnalysis::LookupResultKind;
    const auto Result = VLA->query(Var);
    if (Result.Kind != ResultKind::Defined || !Result.Def ||
        !Result.Def->syntax())
      return Stripped;

    const Node *BindingNode = PM.upTo(*Result.Def->syntax(), Node::NK_Binding);
    if (!BindingNode)
      return Stripped;

    const auto &DefBinding = static_cast<const Binding &>(*BindingNode);
    if (!DefBinding.value() || DefBinding.value().get() == &Current)
      return Stripped;

    return Self(*DefBinding.value(), Depth - 1, Self);
  };

  return Resolve(Value, MaxConstResolveDepth, Resolve);
}

std::optional<std::string>
option_diagnostics::literalString(const Expr &Value) {
  const Expr &Stripped = stripParens(Value);
  if (Stripped.kind() != Node::NK_ExprString)
    return std::nullopt;
  const auto &String = static_cast<const ExprString &>(Stripped);
  if (!String.isLiteral())
    return std::nullopt;
  return String.literal();
}

std::optional<bool> option_diagnostics::literalBool(const Expr &Value) {
  const Expr &Stripped = stripParens(Value);
  if (Stripped.kind() != Node::NK_ExprVar)
    return std::nullopt;
  const std::string &Name = static_cast<const ExprVar &>(Stripped).id().name();
  if (Name == "true")
    return true;
  if (Name == "false")
    return false;
  return std::nullopt;
}

bool option_diagnostics::literalNull(const Expr &Value) {
  const Expr &Stripped = stripParens(Value);
  return Stripped.kind() == Node::NK_ExprVar &&
         static_cast<const ExprVar &>(Stripped).id().name() == "null";
}

std::optional<std::string>
option_diagnostics::pathLiteralText(const Expr &Value) {
  const Expr &Stripped = stripParens(Value);
  if (Stripped.kind() == Node::NK_ExprPath) {
    const auto &Path = static_cast<const ExprPath &>(Stripped);
    if (Path.parts().isLiteral())
      return Path.parts().literal();
    return std::nullopt;
  }
  if (Stripped.kind() == Node::NK_ExprSPath)
    return static_cast<const ExprSPath &>(Stripped).text();
  return std::nullopt;
}

bool option_diagnostics::hasInheritBinding(const ExprAttrs &Attrs) {
  if (!Attrs.binds())
    return false;
  for (const auto &Node : Attrs.binds()->bindings()) {
    if (Node && Node->kind() == Node::NK_Inherit)
      return true;
  }
  return false;
}

bool option_diagnostics::isLetDefinitionBinding(const Binding &Binding,
                                                const ParentMapAnalysis &PM) {
  const Node *AttrsNode = PM.upTo(Binding, Node::NK_ExprAttrs);
  if (!AttrsNode)
    return false;
  const Node *Parent = PM.query(*AttrsNode);
  if (!Parent || Parent->kind() != Node::NK_ExprLet)
    return false;
  return static_cast<const ExprLet &>(*Parent).attrs() == AttrsNode;
}

NixdDiagnostic option_diagnostics::makeTypeDiagnostic(
    const Expr &Value, const std::vector<std::string> &Scope,
    OptionLiteralKind Actual, const OptionType &Expected) {
  return NixdDiagnostic{
      .Range = Value.range(),
      .Severity = NixdDiagnosticSeverity::Error,
      .Code = "option-value-type",
      .Source = "nixd",
      .Message = "value for option `" + renderScope(Scope) + "` has type `" +
                 optionLiteralKindName(Actual) + "`, expected `" +
                 renderExpected(Expected) + "`",
  };
}

NixdDiagnostic option_diagnostics::makeUnknownDiagnostic(
    const Node &Key, const std::vector<std::string> &Scope) {
  return NixdDiagnostic{
      .Range = Key.range(),
      .Severity = NixdDiagnosticSeverity::Error,
      .Code = "option-unknown",
      .Source = "nixd",
      .Message = "unknown option `" + renderScope(Scope) + "`",
  };
}

NixdDiagnostic option_diagnostics::makeRequiredDiagnostic(
    const Expr &Value, const std::vector<std::string> &Scope) {
  return NixdDiagnostic{
      .Range = Value.range(),
      .Severity = NixdDiagnosticSeverity::Warning,
      .Code = "option-required-missing",
      .Source = "nixd",
      .Message = "required option `" + renderScope(Scope) + "` is missing",
  };
}

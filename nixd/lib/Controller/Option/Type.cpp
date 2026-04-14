#include "nixd/Controller/Option.h"
#include "Controller/AST.h"
#include "Integer.h"

#include <nixf/Basic/Nodes/Attrs.h>
#include <nixf/Basic/Nodes/Lambda.h>

#include <algorithm>
#include <cctype>
#include <sstream>

using namespace nixd;
using namespace nixf;

namespace {

std::string toLowerCopy(std::string_view S) {
  std::string Lower;
  Lower.reserve(S.size());
  for (char C : S)
    Lower.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(C))));
  return Lower;
}

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

void markAccepted(ParsedOptionType &Parsed, OptionLiteralKind Kind) {
  Parsed.Accepted.insert(Kind);
  Parsed.Coverage = OptionTypeCoverage::Complete;
}

std::string_view trimOuterParens(std::string_view S) {
  if (S.size() < 2 || S.front() != '(' || S.back() != ')')
    return S;
  return S.substr(1, S.size() - 2);
}

void classifyByName(std::string_view Name, ParsedOptionType &Parsed) {
  if (Name == "bool" || Name == "boolean") {
    markAccepted(Parsed, OptionLiteralKind::Bool);
    return;
  }
  if (option_integer::classifyByName(Name, Parsed))
    return;
  if (Name == "float") {
    markAccepted(Parsed, OptionLiteralKind::Float);
    return;
  }
  if (Name == "str" || Name == "string" || Name == "lines" ||
      Name == "separatedstring") {
    markAccepted(Parsed, OptionLiteralKind::String);
    return;
  }
  if (Name == "path") {
    markAccepted(Parsed, OptionLiteralKind::Path);
    return;
  }
  if (Name == "attrs" || Name == "attrset" || Name == "attrsof" ||
      Name == "attrswith" || Name == "submodule" || Name == "submodulewith") {
    markAccepted(Parsed, OptionLiteralKind::AttrSet);
    return;
  }
  if (Name == "list" || Name == "listof" || Name == "loaof") {
    markAccepted(Parsed, OptionLiteralKind::List);
    return;
  }
  if (Name == "functionto") {
    markAccepted(Parsed, OptionLiteralKind::Function);
    return;
  }
}

void classifyByStableDescription(std::string_view Description,
                                 ParsedOptionType &Parsed) {
  const std::string LowerStorage = toLowerCopy(Description);
  std::string_view Lower = trimOuterParens(LowerStorage);
  if (Lower == "boolean" || Lower == "boolean value")
    markAccepted(Parsed, OptionLiteralKind::Bool);
  option_integer::classifyByStableDescription(Lower, Parsed);
  if (Lower == "floating point number")
    markAccepted(Parsed, OptionLiteralKind::Float);
  if (Lower == "string")
    markAccepted(Parsed, OptionLiteralKind::String);
  if (Lower == "path")
    markAccepted(Parsed, OptionLiteralKind::Path);
  if (Lower == "absolute path") {
    markAccepted(Parsed, OptionLiteralKind::Path);
    Parsed.AcceptsAbsolutePathString = true;
  }
  if (Lower == "attribute set")
    markAccepted(Parsed, OptionLiteralKind::AttrSet);
  if (Lower.starts_with("attribute set of "))
    markAccepted(Parsed, OptionLiteralKind::AttrSet);
  if (Lower.starts_with("list of ") || Lower.starts_with("non-empty list of "))
    markAccepted(Parsed, OptionLiteralKind::List);
  if (Lower == "submodule")
    markAccepted(Parsed, OptionLiteralKind::AttrSet);
}

bool isAbsolutePathString(const Expr &Value) {
  if (Value.kind() != Node::NK_ExprString)
    return false;
  const auto &String = static_cast<const ExprString &>(Value);
  return String.isLiteral() && String.literal().starts_with("/");
}

bool isStaticString(const ExprString &String) {
  return String.isLiteral() || String.parts().fragments().empty();
}

std::optional<std::vector<std::string>>
staticBindingPath(const Binding &Binding) {
  std::vector<std::string> Path;
  for (const auto &Name : Binding.path().names()) {
    if (!Name || !Name->isStatic())
      return std::nullopt;
    Path.emplace_back(Name->staticName());
  }
  if (Path.empty())
    return std::nullopt;
  return Path;
}

bool isListElementChild(const ExprList &List, const Node &Child) {
  for (const auto &Element : List.elements())
    if (Element.get() == &Child)
      return true;
  return false;
}

} // namespace

bool ParsedOptionType::accepts(OptionLiteralKind Kind) const {
  return (Kind == OptionLiteralKind::Null && AllowNull) ||
         Accepted.contains(Kind);
}

bool ParsedOptionType::acceptsBoolean() const {
  return Accepted.contains(OptionLiteralKind::Bool);
}

std::optional<ParsedOptionType> nixd::parseOptionType(const OptionType &Type) {
  ParsedOptionType Parsed;
  Parsed.Rendered = renderOptionType(Type);
  if (Parsed.Rendered.empty())
    return std::nullopt;

  const std::string Name = Type.Name ? toLowerCopy(*Type.Name) : "";
  Parsed.AllowNull = Name == "nullor";
  if (Parsed.AllowNull)
    Parsed.Coverage = OptionTypeCoverage::Partial;
  classifyByName(Name, Parsed);

  if (Type.Description) {
    const std::string LowerDescription = toLowerCopy(*Type.Description);
    option_integer::refineFromDescription(Name, LowerDescription, Parsed);
    if (Name == "nullor" && LowerDescription.starts_with("null or "))
      classifyByStableDescription(LowerDescription.substr(8), Parsed);
    else if (Name.empty())
      classifyByStableDescription(LowerDescription, Parsed);
  }

  if (Parsed.Coverage != OptionTypeCoverage::Complete)
    return std::nullopt;
  return Parsed;
}

OptionLiteralKind nixd::classifyOptionLiteral(const Expr &Value) {
  using NK = Node::NodeKind;
  switch (Value.kind()) {
  case NK::NK_ExprParen: {
    const auto &Paren = static_cast<const ExprParen &>(Value);
    if (const nixf::Expr *Inner = Paren.expr())
      return classifyOptionLiteral(*Inner);
    return OptionLiteralKind::Unknown;
  }
  case NK::NK_ExprInt:
    return OptionLiteralKind::Int;
  case NK::NK_ExprUnaryOp:
    return option_integer::constantValue(Value) ? OptionLiteralKind::Int
                                                : OptionLiteralKind::Unknown;
  case NK::NK_ExprFloat:
    return OptionLiteralKind::Float;
  case NK::NK_ExprString: {
    const auto &String = static_cast<const ExprString &>(Value);
    return isStaticString(String) ? OptionLiteralKind::String
                                  : OptionLiteralKind::Unknown;
  }
  case NK::NK_ExprPath:
  case NK::NK_ExprSPath:
    return OptionLiteralKind::Path;
  case NK::NK_ExprList:
    return OptionLiteralKind::List;
  case NK::NK_ExprAttrs:
    return OptionLiteralKind::AttrSet;
  case NK::NK_ExprLambda:
    return OptionLiteralKind::Function;
  case NK::NK_ExprVar: {
    const auto &Var = static_cast<const ExprVar &>(Value);
    if (Var.id().name() == "null")
      return OptionLiteralKind::Null;
    if (Var.id().name() == "true" || Var.id().name() == "false")
      return OptionLiteralKind::Bool;
    return OptionLiteralKind::Unknown;
  }
  default:
    return OptionLiteralKind::Unknown;
  }
}

OptionValueMatch nixd::optionValueMatch(const ParsedOptionType &Expected,
                                        const Expr &Value,
                                        OptionLiteralKind Actual) {
  if (Expected.Coverage != OptionTypeCoverage::Complete ||
      Actual == OptionLiteralKind::Unknown)
    return OptionValueMatch::Unknown;

  if (Expected.accepts(Actual)) {
    if (Actual == OptionLiteralKind::Int)
      return option_integer::matchConstraint(Expected, Value);
    return OptionValueMatch::Matches;
  }

  if (Actual == OptionLiteralKind::String &&
      Expected.accepts(OptionLiteralKind::Path)) {
    if (!Expected.AcceptsAbsolutePathString)
      return OptionValueMatch::Unknown;
    return isAbsolutePathString(Value) ? OptionValueMatch::Matches
                                       : OptionValueMatch::Mismatches;
  }

  return OptionValueMatch::Mismatches;
}

std::string nixd::optionLiteralKindName(OptionLiteralKind Kind) {
  switch (Kind) {
  case OptionLiteralKind::Null:
    return "null";
  case OptionLiteralKind::Bool:
    return "boolean";
  case OptionLiteralKind::Int:
    return "integer";
  case OptionLiteralKind::Float:
    return "float";
  case OptionLiteralKind::String:
    return "string";
  case OptionLiteralKind::Path:
    return "path";
  case OptionLiteralKind::List:
    return "list";
  case OptionLiteralKind::AttrSet:
    return "attribute set";
  case OptionLiteralKind::Function:
    return "function";
  default:
    return "unknown";
  }
}

std::optional<std::vector<std::string>>
nixd::findOptionBindingScope(const Binding &Binding,
                             const ParentMapAnalysis &PM) {
  const auto &Names = Binding.path().names();
  if (Names.empty())
    return std::nullopt;

  std::vector<std::string> Scope;
  if (findAttrPathForOptions(*Names.back(), PM, Scope) !=
          FindAttrPathResult::OK ||
      Scope.empty())
    return std::nullopt;
  return Scope;
}

std::optional<OptionValueContext>
nixd::findOptionValueContext(const Node &Desc, const ParentMapAnalysis &PM,
                             Position Pos) {
  const nixf::Node *BindingNode = PM.upTo(Desc, nixf::Node::NK_Binding);
  if (!BindingNode)
    return std::nullopt;

  const auto &OuterBinding = static_cast<const nixf::Binding &>(*BindingNode);
  if (!OuterBinding.eq())
    return std::nullopt;

  if (Pos < OuterBinding.eq()->rCur().position())
    return std::nullopt;
  if (OuterBinding.value() && OuterBinding.value()->rCur().position() < Pos)
    return std::nullopt;

  std::optional<std::vector<std::string>> Scope =
      findOptionBindingScope(OuterBinding, PM);
  if (!Scope)
    return std::nullopt;

  std::vector<OptionValueChildStep> ReversedPath;
  const Expr *CompletionExpr = static_cast<const Expr *>(PM.upExpr(Desc));
  const Node *Current = &Desc;
  const Expr *OuterValue = OuterBinding.value().get();
  while (Current && Current != OuterValue) {
    if (PM.isRoot(*Current))
      break;
    const Node *Parent = PM.query(*Current);
    if (!Parent)
      break;

    if (Parent->kind() == Node::NK_ExprList &&
        isListElementChild(static_cast<const ExprList &>(*Parent), *Current)) {
      ReversedPath.push_back(
          OptionValueChildStep{.Kind = OptionValueChildKind::ListElement});
    } else if (Parent->kind() == Node::NK_Binding &&
               Parent != BindingNode &&
               static_cast<const Binding *>(Parent)->value().get() ==
                   Current) {
      if (std::optional<std::vector<std::string>> Path =
              staticBindingPath(static_cast<const Binding &>(*Parent))) {
        for (auto It = Path->rbegin(); It != Path->rend(); ++It)
          ReversedPath.push_back(OptionValueChildStep{
              .Kind = OptionValueChildKind::AttrValue, .Name = *It});
      }
    } else if (Parent->kind() == Node::NK_ExprLambda &&
               static_cast<const ExprLambda *>(Parent)->body() == Current) {
      ReversedPath.push_back(
          OptionValueChildStep{.Kind = OptionValueChildKind::FunctionBody});
    }

    Current = Parent;
  }

  std::reverse(ReversedPath.begin(), ReversedPath.end());
  return OptionValueContext{.Binding = &OuterBinding,
                            .CompletionExpr = CompletionExpr,
                            .Pos = Pos,
                            .Scope = std::move(*Scope),
                            .ValuePath = std::move(ReversedPath)};
}

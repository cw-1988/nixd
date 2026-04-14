#include "Controller/Completion/Value.h"

#include "Controller/Convert.h"
#include "Controller/Option/DiagnosticsSupport.h"

#include <cctype>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace nixd;
using namespace lspserver;
using namespace nixf;

namespace nixd::completion::options::value {
namespace {

option_navigation::ChildKind toNavigationKind(OptionValueChildKind Kind) {
  switch (Kind) {
  case OptionValueChildKind::ListElement:
    return option_navigation::ChildKind::ListElement;
  case OptionValueChildKind::AttrValue:
    return option_navigation::ChildKind::AttrValue;
  case OptionValueChildKind::FunctionBody:
    return option_navigation::ChildKind::FunctionBody;
  }
  return option_navigation::ChildKind::AttrValue;
}

bool completeForType(const OptionValueContext &Context, const OptionType &Type,
                     const std::string &Prefix, llvm::StringRef Src,
                     bool ClientSupportSnippet, AttrSetClient *NixpkgsClient,
                     std::set<std::string> &SeenLabels,
                     std::vector<CompletionItem> &Items) {
  bool Handled = false;
  Handled |= completeEnum(Context, Type, Prefix, Src, SeenLabels, Items);
  Handled |= completeBoolean(Context, Type, Prefix, Src, SeenLabels, Items);
  Handled |= completePath(Context, Type, Prefix, Src, SeenLabels, Items);
  Handled |= completePackage(Context, Type, Prefix, Src, NixpkgsClient,
                             SeenLabels, Items);

  const std::string LowerName = Type.Name ? lowerCopy(*Type.Name) : "";
  if (LowerName == "nullor") {
    Handled |= completeNull(Context, Prefix, Src, SeenLabels, Items);
    if (std::optional<OptionType> Elem = elemTypeFor(Type))
      Handled |= completeForType(Context, *Elem, Prefix, Src,
                                 ClientSupportSnippet, NixpkgsClient,
                                 SeenLabels, Items);
  } else if (LowerName == "unique") {
    if (std::optional<OptionType> Elem = elemTypeFor(Type))
      Handled |= completeForType(Context, *Elem, Prefix, Src,
                                 ClientSupportSnippet, NixpkgsClient,
                                 SeenLabels, Items);
  }

  for (const OptionType &Alternative : alternativeTypesFor(Type, LowerName))
    Handled |= completeForType(Context, Alternative, Prefix, Src,
                               ClientSupportSnippet, NixpkgsClient, SeenLabels,
                               Items);

  if (!Handled)
    Handled |= completeSnippet(Context, Type, Prefix, Src, ClientSupportSnippet,
                               SeenLabels, Items);

  return Handled;
}

} // namespace

std::string lowerCopy(std::string_view S) {
  std::string Lower;
  Lower.reserve(S.size());
  for (char C : S)
    Lower.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(C))));
  return Lower;
}

std::vector<option_navigation::ChildStep>
navigationPath(const OptionValueContext &Context) {
  std::vector<option_navigation::ChildStep> Path;
  Path.reserve(Context.ValuePath.size());
  for (const OptionValueChildStep &Step : Context.ValuePath)
    Path.push_back(option_navigation::ChildStep{
        .Kind = toNavigationKind(Step.Kind), .Name = Step.Name});
  return Path;
}

std::string prefix(const OptionValueContext &Context) {
  const Expr *Value = Context.CompletionExpr;
  if (!Value && Context.Binding)
    Value = Context.Binding->value().get();
  if (!Value)
    return "";

  if (Value->kind() == Node::NK_ExprVar)
    return static_cast<const ExprVar *>(Value)->id().name();

  if (Value->kind() == Node::NK_ExprString) {
    const auto *String = static_cast<const ExprString *>(Value);
    if (String->isLiteral())
      return String->literal();
  }

  if (std::optional<std::string> Path =
          option_diagnostics::pathLiteralText(*Value))
    return *Path;

  return "";
}

void addInsertEdits(const OptionValueContext &Context, llvm::StringRef Src,
                    std::string_view Value, CompletionItem &Item) {
  const Expr *Expr = Context.CompletionExpr;
  if (!Expr)
    return;

  if (Expr->kind() != Node::NK_ExprString)
    return;
  Item.textEdit = lspserver::TextEdit{
      .range = toLSPRange(Src, Expr->range()),
      .newText = std::string(Value),
  };
}

bool isSelectContext(const OptionValueContext &Context) {
  return Context.CompletionExpr &&
         Context.CompletionExpr->kind() == Node::NK_ExprSelect;
}

std::optional<OptionType> elemTypeFor(const OptionType &Type) {
  return option_navigation::elemTypeFor(Type,
                                        option_navigation::lowerTypeName(Type));
}

std::vector<OptionType> alternativeTypesFor(const OptionType &Type,
                                            std::string_view LowerName) {
  return option_navigation::alternativeTypesFor(Type, LowerName);
}

void complete(const OptionValueContext &Context,
              const std::vector<ResolvedOptionInfo> &Infos,
              bool CompletionSnippets, AttrSetClient *NixpkgsClient,
              llvm::StringRef Src, std::vector<CompletionItem> &Items) {
  const std::string Prefix = prefix(Context);
  std::set<std::string> SeenLabels;
  const std::vector<option_navigation::ChildStep> ValuePath =
      navigationPath(Context);
  for (const ResolvedOptionInfo &Info : Infos) {
    if (!Info.Description.Type)
      continue;

    std::vector<OptionType> Expected =
        option_navigation::descendValuePath(*Info.Description.Type, ValuePath);
    for (const OptionType &Type : Expected) {
      if (completeForType(Context, Type, Prefix, Src, CompletionSnippets,
                          NixpkgsClient, SeenLabels, Items))
        return;
    }
  }
}

} // namespace nixd::completion::options::value

void nixd::completion::completeOptionValue(
    const OptionValueContext &Context,
    const std::vector<ResolvedOptionInfo> &Infos, bool CompletionSnippets,
    AttrSetClient *NixpkgsClient, llvm::StringRef Src,
    std::vector<CompletionItem> &Items) {
  options::value::complete(Context, Infos, CompletionSnippets, NixpkgsClient,
                           Src, Items);
}

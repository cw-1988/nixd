#include "Controller/Completion/Value.h"

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

lspserver::Range cursorRange(const OptionValueContext &Context) {
  lspserver::Position Pos{.line = Context.Pos.line(),
                          .character = Context.Pos.column()};
  return lspserver::Range{.start = Pos, .end = Pos};
}

std::string placeholderForType(const OptionType &Type) {
  const std::string LowerName = Type.Name ? lowerCopy(*Type.Name) : "";
  if (LowerName == "bool")
    return "true";
  if (LowerName == "int" || LowerName == "integer" || LowerName == "port")
    return "0";
  if (LowerName == "float")
    return "0.0";
  if (LowerName == "listof" || LowerName == "list")
    return "[ ]";
  if (LowerName == "functionto")
    return "args: value";
  if (typeAcceptsPath(Type))
    return "./";
  if (typeAcceptsPackage(Type))
    return "pkgs.hello";
  return "\"\"";
}

std::string escape(std::string_view Text) {
  std::string Out;
  Out.reserve(Text.size());
  for (char C : Text) {
    if (C == '\\' || C == '$' || C == '}')
      Out.push_back('\\');
    Out.push_back(C);
  }
  return Out;
}

std::optional<std::string> submoduleSnippet(const OptionType &Type,
                                            bool Snippets) {
  if (!option_navigation::isSubmoduleLike(Type))
    return std::nullopt;

  std::vector<std::pair<std::string, OptionType>> Required;
  for (const auto &[Name, Summary] : Type.KnownSubOptions) {
    if (!Summary.Required)
      continue;
    if (std::optional<OptionType> Child =
            option_navigation::nestedType(Type, Name))
      Required.emplace_back(Name, std::move(*Child));
  }

  if (Required.empty())
    return std::string("{ }");

  std::string Text = "{\n";
  int Index = 1;
  for (const auto &[Name, Child] : Required) {
    std::string Placeholder = placeholderForType(Child);
    Text += "  " + Name + " = ";
    if (Snippets)
      Text += "${" + std::to_string(Index++) + ":" + escape(Placeholder) + "}";
    else
      Text += Placeholder;
    Text += ";\n";
  }
  Text += "}";
  return Text;
}

} // namespace

bool completeSnippet(const OptionValueContext &Context, const OptionType &Type,
                     const std::string &Prefix, llvm::StringRef Src,
                     bool ClientSupportSnippet,
                     std::set<std::string> &SeenLabels,
                     std::vector<CompletionItem> &Items) {
  if (!Prefix.empty() || isSelectContext(Context))
    return false;

  std::optional<std::string> InsertText =
      submoduleSnippet(Type, ClientSupportSnippet);
  std::string Label = "attrset";
  CompletionItemKind Kind = CompletionItemKind::Snippet;
  if (!InsertText) {
    const std::optional<ParsedOptionType> Parsed = parseOptionType(Type);
    if (!Parsed)
      return false;
    if (Parsed->Accepted.contains(OptionLiteralKind::String)) {
      Label = "\"\"";
      InsertText = ClientSupportSnippet ? "\"${1:value}\"" : "\"\"";
    } else if (Parsed->Accepted.contains(OptionLiteralKind::Int)) {
      Label = "0";
      InsertText = ClientSupportSnippet ? "${1:0}" : "0";
    } else if (Parsed->Accepted.contains(OptionLiteralKind::Float)) {
      Label = "0.0";
      InsertText = ClientSupportSnippet ? "${1:0.0}" : "0.0";
    } else if (Parsed->Accepted.contains(OptionLiteralKind::List)) {
      Label = "[ ]";
      InsertText = ClientSupportSnippet ? "[ ${1} ]" : "[ ]";
    } else if (Parsed->Accepted.contains(OptionLiteralKind::AttrSet)) {
      Label = "{ }";
      InsertText = ClientSupportSnippet ? "{\n  ${1}\n}" : "{ }";
    } else if (Parsed->Accepted.contains(OptionLiteralKind::Function)) {
      Label = "args: value";
      InsertText =
          ClientSupportSnippet ? "${1:args}: ${2:value}" : "args: value";
    } else {
      return false;
    }
    Kind = CompletionItemKind::Snippet;
  } else {
    Label = *InsertText == "{ }" ? "{ }" : "required fields";
  }

  if (SeenLabels.contains(Label))
    return false;

  CompletionItem Item{
      .label = Label,
      .kind = Kind,
      .detail = "option value snippet",
      .insertText = *InsertText,
      .insertTextFormat = ClientSupportSnippet ? InsertTextFormat::Snippet
                                               : InsertTextFormat::PlainText,
  };
  if (Context.CompletionExpr &&
      Context.CompletionExpr->kind() == Node::NK_ExprString)
    addInsertEdits(Context, Src, *InsertText, Item);
  else
    Item.textEdit = lspserver::TextEdit{.range = cursorRange(Context),
                                        .newText = *InsertText};
  SeenLabels.insert(Label);
  addItem(Items, std::move(Item));
  return true;
}

} // namespace nixd::completion::options::value

#include "CompletionOptions.h"
#include "AST.h"
#include "Convert.h"

#include "lspserver/Protocol.h"

#include "nixd/Controller/Option.h"
#include "nixd/Protocol/AttrSet.h"

#include <llvm/Support/FormatVariadic.h>

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

using namespace nixd;
using namespace lspserver;
using namespace nixf;

namespace nixd::completion {
namespace {

constexpr CompletionItemKind OptionKind = CompletionItemKind::Constructor;
constexpr CompletionItemKind OptionAttrKind = CompletionItemKind::Class;

bool isBooleanOption(const OptionDescription &Desc) {
  if (!Desc.Type)
    return false;
  const std::optional<ParsedOptionType> Parsed = parseOptionType(*Desc.Type);
  return Parsed && Parsed->acceptsBoolean();
}

/// \brief Provide completion list by nixpkgs module system (options).
class OptionCompletionProvider {
  // Whether the client supports code snippets.
  bool ClientSupportSnippet;

  static std::string escapeCharacters(const std::set<char> &Charset,
                                      const std::string &Origin) {
    // Escape characters listed in charset.
    std::string Ret;
    Ret.reserve(Origin.size());
    for (const auto Ch : Origin) {
      if (Charset.contains(Ch)) {
        Ret += "\\";
        Ret += Ch;
      } else {
        Ret += Ch;
      }
    }
    return Ret;
  }

  void fillInsertText(CompletionItem &Item, const std::string &Name,
                      const OptionDescription &Desc) const {
    std::string Example = Desc.Example.value_or("");
    if (Example.empty() && isBooleanOption(Desc))
      Example = "true";

    if (!ClientSupportSnippet) {
      Item.insertTextFormat = InsertTextFormat::PlainText;
      Item.insertText = Name + " = " + Example + ";";
      return;
    }
    Item.insertTextFormat = InsertTextFormat::Snippet;
    Item.insertText = Name + " = " +
                      "${1:" + escapeCharacters({'\\', '$', '}'}, Example) +
                      "}" + ";";
  }

public:
  OptionCompletionProvider(bool ClientSupportSnippet)
      : ClientSupportSnippet(ClientSupportSnippet) {}

  void completeOptions(const std::vector<ResolvedOptionField> &Fields,
                       std::vector<CompletionItem> &Items) {
    for (const ResolvedOptionField &Resolved : Fields) {
      const nixd::OptionField &Field = Resolved.Field;
      CompletionItem Item;

      Item.label = Field.Name;
      Item.detail = Resolved.ProviderName;

      if (Field.Description) {
        const OptionDescription &Desc = *Field.Description;
        Item.kind = OptionKind;
        fillInsertText(Item, Field.Name, Desc);
        Item.documentation = MarkupContent{
            .kind = MarkupKind::Markdown,
            .value = Desc.Description.value_or(""),
        };
        Item.detail += " | "; // separator between origin and type desc.
        if (Desc.Type) {
          std::string TypeName = Desc.Type->Name.value_or("");
          std::string TypeDesc = Desc.Type->Description.value_or("");
          Item.detail += llvm::formatv("{0} ({1})", TypeName, TypeDesc);
        } else {
          Item.detail += "? (missing type)";
        }
        addItem(Items, std::move(Item));
      } else {
        Item.kind = OptionAttrKind;
        addItem(Items, std::move(Item));
      }
    }
  }
};

std::string optionValuePrefix(const OptionValueContext &Context) {
  const auto &Value = Context.Binding->value();
  if (!Value)
    return "";

  if (Value->kind() == Node::NK_ExprVar)
    return static_cast<const ExprVar &>(*Value).id().name();

  if (Value->kind() == Node::NK_ExprString) {
    const auto &String = static_cast<const ExprString &>(*Value);
    if (String.isLiteral())
      return String.literal();
  }

  return "";
}

void addOptionValueInsertEdits(const OptionValueContext &Context,
                               llvm::StringRef Src, std::string_view Value,
                               CompletionItem &Item) {
  const auto &Expr = Context.Binding->value();
  if (!Expr || Expr->kind() != Node::NK_ExprString)
    return;

  Item.textEdit = lspserver::TextEdit{
      .range = toLSPRange(Src, Expr->range()),
      .newText = std::string(Value),
  };
}

} // namespace

void completeOptionNames(const std::vector<ResolvedOptionField> &Fields,
                         bool CompletionSnippets,
                         std::vector<CompletionItem> &List) {
  OptionCompletionProvider OCP(CompletionSnippets);
  OCP.completeOptions(Fields, List);
}

std::optional<AttrPathCompleteParams>
optionAttrPathCompletionParams(const Node &N, const ParentMapAnalysis &PM) {
  std::vector<std::string> Scope;
  using PathResult = FindAttrPathResult;
  auto R = findAttrPathForOptions(N, PM, Scope);
  if (R != PathResult::OK || Scope.empty())
    return std::nullopt;

  std::string Prefix = Scope.back();
  Scope.pop_back();
  return AttrPathCompleteParams{.Scope = std::move(Scope),
                                .Prefix = std::move(Prefix)};
}

void completeOptionValue(const OptionValueContext &Context,
                         const std::vector<ResolvedOptionInfo> &Infos,
                         llvm::StringRef Src,
                         std::vector<CompletionItem> &Items) {
  const std::string Prefix = optionValuePrefix(Context);
  for (const ResolvedOptionInfo &Info : Infos) {
    if (!isBooleanOption(Info.Description))
      continue;
    for (std::string_view Value : {"true", "false"}) {
      if (!Value.starts_with(Prefix))
        continue;
      CompletionItem Item{
          .label = std::string(Value),
          .kind = CompletionItemKind::Keyword,
          .detail = "boolean option value",
      };
      addOptionValueInsertEdits(Context, Src, Value, Item);
      addItem(Items, std::move(Item));
    }
    return;
  }
}

} // namespace nixd::completion

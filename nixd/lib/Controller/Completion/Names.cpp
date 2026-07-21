#include "Controller/Completion/Options.h"

#include <llvm/Support/FormatVariadic.h>

#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace nixd;
using namespace lspserver;

namespace nixd::completion::options::names {
namespace {

constexpr CompletionItemKind OptionKind = CompletionItemKind::Constructor;
constexpr CompletionItemKind OptionAttrKind = CompletionItemKind::Class;

bool isBooleanOption(const OptionDescription &Desc) {
  if (!Desc.Type)
    return false;
  const std::optional<ParsedOptionType> Parsed = parseOptionType(*Desc.Type);
  return Parsed && Parsed->acceptsBoolean();
}

std::string escapeCharacters(const std::set<char> &Charset,
                             const std::string &Origin) {
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

class Provider {
  bool ClientSupportSnippet;

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
  Provider(bool ClientSupportSnippet)
      : ClientSupportSnippet(ClientSupportSnippet) {}

  void complete(const std::vector<ResolvedOptionField> &Fields,
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
        Item.detail += " | ";
        if (Desc.Type) {
          std::string TypeName = Desc.Type->Name.value_or("");
          std::string TypeDesc = Desc.Type->Description.value_or("");
          Item.detail += llvm::formatv("{0} ({1})", TypeName, TypeDesc);
        } else {
          Item.detail += "? (missing type)";
        }
        Items.emplace_back(std::move(Item));
      } else {
        Item.kind = OptionAttrKind;
        Items.emplace_back(std::move(Item));
      }
    }
  }
};

} // namespace

void complete(const std::vector<ResolvedOptionField> &Fields,
              bool CompletionSnippets, std::vector<CompletionItem> &List) {
  Provider OCP(CompletionSnippets);
  OCP.complete(Fields, List);
}

} // namespace nixd::completion::options::names

void nixd::completion::completeOptionNames(
    const std::vector<ResolvedOptionField> &Fields, bool CompletionSnippets,
    std::vector<CompletionItem> &List) {
  options::names::complete(Fields, CompletionSnippets, List);
}

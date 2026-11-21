#include "Controller/Completion/Options.h"
#include "Controller/Completion/Snippet.h"

#include <llvm/Support/FormatVariadic.h>

#include <optional>
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

class Provider {
  bool ClientSupportSnippet;

  static std::string resolveData(const ResolvedOptionField &Resolved,
                                 const std::vector<std::string> &Scope) {
    return llvm::formatv("{0}", llvm::json::Value(llvm::json::Object{
                                    {"Kind", "option"},
                                    {"Provider", Resolved.ProviderName},
                                    {"Scope", Scope},
                                }));
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
    Item.insertText =
        Name + " = " + "${1:" + escapeSnippet(Example) + "}" + ";";
  }

public:
  Provider(bool ClientSupportSnippet)
      : ClientSupportSnippet(ClientSupportSnippet) {}

  void complete(const std::vector<ResolvedOptionField> &Fields,
                const std::vector<std::string> &Scope,
                std::vector<CompletionItem> &Items) {
    for (const ResolvedOptionField &Resolved : Fields) {
      const nixd::OptionField &Field = Resolved.Field;
      CompletionItem Item;

      Item.label = Field.Name;
      Item.detail = Resolved.ProviderName;

      if (Field.Description) {
        const OptionDescription &Desc = *Field.Description;
        Item.data = resolveData(Resolved, Scope);
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
              const std::vector<std::string> &Scope, bool CompletionSnippets,
              std::vector<CompletionItem> &List) {
  Provider OCP(CompletionSnippets);
  OCP.complete(Fields, Scope, List);
}

} // namespace nixd::completion::options::names

void nixd::completion::completeOptionNames(
    const std::vector<ResolvedOptionField> &Fields,
    const std::vector<std::string> &Scope, bool CompletionSnippets,
    std::vector<CompletionItem> &List) {
  options::names::complete(Fields, Scope, CompletionSnippets, List);
}

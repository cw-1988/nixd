#include "CompletionOptions.h"
#include "AST.h"
#include "Convert.h"

#include "lspserver/Protocol.h"

#include "nixd/Controller/Option.h"
#include "nixd/Protocol/AttrSet.h"

#include <llvm/Support/FormatVariadic.h>

#include <cctype>
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

std::string escapeNixString(std::string_view Origin) {
  std::string Ret;
  Ret.reserve(Origin.size());
  for (size_t I = 0; I < Origin.size(); ++I) {
    char Ch = Origin[I];
    switch (Ch) {
    case '\\':
      Ret += "\\\\";
      break;
    case '"':
      Ret += "\\\"";
      break;
    case '\n':
      Ret += "\\n";
      break;
    case '\r':
      Ret += "\\r";
      break;
    case '\t':
      Ret += "\\t";
      break;
    case '$':
      if (I + 1 < Origin.size() && Origin[I + 1] == '{')
        Ret += "\\$";
      else
        Ret += Ch;
      break;
    default:
      Ret += Ch;
      break;
    }
  }
  return Ret;
}

struct EnumCompletionValue {
  std::string Label;
  std::string FilterText;
};

std::optional<EnumCompletionValue>
enumCompletionValue(const OptionType::EnumValue &Value) {
  if (Value.String) {
    std::string Quoted = "\"" + escapeNixString(*Value.String) + "\"";
    return EnumCompletionValue{.Label = std::move(Quoted),
                               .FilterText = *Value.String};
  }
  if (Value.Integer) {
    std::string Text = std::to_string(*Value.Integer);
    return EnumCompletionValue{.Label = Text, .FilterText = std::move(Text)};
  }
  if (Value.Boolean) {
    std::string Text = *Value.Boolean ? "true" : "false";
    return EnumCompletionValue{.Label = Text, .FilterText = std::move(Text)};
  }
  if (Value.IsNull)
    return EnumCompletionValue{.Label = "null", .FilterText = "null"};
  return std::nullopt;
}

std::string toLowerCopy(std::string_view S) {
  std::string Lower;
  Lower.reserve(S.size());
  for (char C : S)
    Lower.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(C))));
  return Lower;
}

std::optional<OptionType> nestedType(const OptionType &Type,
                                     std::string_view Name) {
  auto It = Type.NestedTypes.find(std::string(Name));
  if (It == Type.NestedTypes.end())
    return std::nullopt;
  return It->second;
}

std::optional<OptionType> elemTypeFor(const OptionType &Type) {
  return nestedType(Type, "elemType");
}

std::vector<OptionType> alternativeTypesFor(const OptionType &Type,
                                            std::string_view LowerName) {
  std::vector<OptionType> Alternatives;
  if (LowerName == "either" || LowerName == "oneof") {
    if (std::optional<OptionType> Left = nestedType(Type, "left"))
      Alternatives.emplace_back(std::move(*Left));
    if (std::optional<OptionType> Right = nestedType(Type, "right"))
      Alternatives.emplace_back(std::move(*Right));
    if (Alternatives.empty())
      for (const auto &Entry : Type.NestedTypes)
        Alternatives.emplace_back(Entry.second);
  } else if (LowerName == "coercedto") {
    if (std::optional<OptionType> Coerced = nestedType(Type, "coercedType"))
      Alternatives.emplace_back(std::move(*Coerced));
    if (std::optional<OptionType> Final = nestedType(Type, "finalType"))
      Alternatives.emplace_back(std::move(*Final));
  }
  return Alternatives;
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

bool completeEnumOptionValue(const OptionValueContext &Context,
                             const OptionType &Type, const std::string &Prefix,
                             llvm::StringRef Src,
                             std::set<std::string> &SeenLabels,
                             std::vector<CompletionItem> &Items) {
  if (Type.EnumValues.empty())
    return false;

  for (const OptionType::EnumValue &Value : Type.EnumValues) {
    std::optional<EnumCompletionValue> Completion = enumCompletionValue(Value);
    if (!Completion || !Completion->FilterText.starts_with(Prefix) ||
        SeenLabels.contains(Completion->Label))
      continue;

    CompletionItem Item{
        .label = Completion->Label,
        .kind = CompletionItemKind::EnumMember,
        .detail = "enum option value",
        .filterText = Completion->FilterText,
    };
    addOptionValueInsertEdits(Context, Src, Completion->Label, Item);
    SeenLabels.insert(Completion->Label);
    addItem(Items, std::move(Item));
  }

  return true;
}

bool completeBooleanOptionValue(const OptionValueContext &Context,
                                const OptionType &Type,
                                const std::string &Prefix, llvm::StringRef Src,
                                std::set<std::string> &SeenLabels,
                                std::vector<CompletionItem> &Items) {
  const std::optional<ParsedOptionType> Parsed = parseOptionType(Type);
  if (!Parsed || !Parsed->acceptsBoolean())
    return false;

  for (std::string_view Value : {"true", "false"}) {
    if (!Value.starts_with(Prefix) || SeenLabels.contains(std::string(Value)))
      continue;
    CompletionItem Item{
        .label = std::string(Value),
        .kind = CompletionItemKind::Keyword,
        .detail = "boolean option value",
    };
    addOptionValueInsertEdits(Context, Src, Value, Item);
    SeenLabels.insert(std::string(Value));
    addItem(Items, std::move(Item));
  }
  return true;
}

bool completeNullOptionValue(const OptionValueContext &Context,
                             const std::string &Prefix, llvm::StringRef Src,
                             std::set<std::string> &SeenLabels,
                             std::vector<CompletionItem> &Items) {
  if (!std::string_view("null").starts_with(Prefix) ||
      SeenLabels.contains("null"))
    return false;

  CompletionItem Item{
      .label = "null",
      .kind = CompletionItemKind::Keyword,
      .detail = "null option value",
  };
  addOptionValueInsertEdits(Context, Src, "null", Item);
  SeenLabels.insert("null");
  addItem(Items, std::move(Item));
  return true;
}

bool completeOptionValueForType(const OptionValueContext &Context,
                                const OptionType &Type,
                                const std::string &Prefix, llvm::StringRef Src,
                                std::set<std::string> &SeenLabels,
                                std::vector<CompletionItem> &Items) {
  bool Handled = false;
  Handled |=
      completeEnumOptionValue(Context, Type, Prefix, Src, SeenLabels, Items);
  Handled |=
      completeBooleanOptionValue(Context, Type, Prefix, Src, SeenLabels, Items);

  const std::string LowerName = Type.Name ? toLowerCopy(*Type.Name) : "";
  if (LowerName == "nullor") {
    Handled |= completeNullOptionValue(Context, Prefix, Src, SeenLabels, Items);
    if (std::optional<OptionType> Elem = elemTypeFor(Type))
      Handled |= completeOptionValueForType(Context, *Elem, Prefix, Src,
                                            SeenLabels, Items);
  } else if (LowerName == "unique") {
    if (std::optional<OptionType> Elem = elemTypeFor(Type))
      Handled |= completeOptionValueForType(Context, *Elem, Prefix, Src,
                                            SeenLabels, Items);
  }

  for (const OptionType &Alternative : alternativeTypesFor(Type, LowerName))
    Handled |= completeOptionValueForType(Context, Alternative, Prefix, Src,
                                          SeenLabels, Items);

  return Handled;
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
  std::set<std::string> SeenLabels;
  for (const ResolvedOptionInfo &Info : Infos) {
    if (!Info.Description.Type)
      continue;

    if (completeOptionValueForType(Context, *Info.Description.Type, Prefix, Src,
                                   SeenLabels, Items))
      return;
  }
}

} // namespace nixd::completion

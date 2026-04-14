#include "Controller/Completion/Value.h"

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace nixd;
using namespace lspserver;

namespace nixd::completion::options::value {
namespace {

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

struct EnumValue {
  std::string Label;
  std::string FilterText;
};

std::optional<EnumValue> completionValue(const OptionType::EnumValue &Value) {
  if (Value.String) {
    std::string Quoted = "\"" + escapeNixString(*Value.String) + "\"";
    return EnumValue{.Label = std::move(Quoted), .FilterText = *Value.String};
  }
  if (Value.Integer) {
    std::string Text = std::to_string(*Value.Integer);
    return EnumValue{.Label = Text, .FilterText = std::move(Text)};
  }
  if (Value.Boolean) {
    std::string Text = *Value.Boolean ? "true" : "false";
    return EnumValue{.Label = Text, .FilterText = std::move(Text)};
  }
  if (Value.IsNull)
    return EnumValue{.Label = "null", .FilterText = "null"};
  return std::nullopt;
}

} // namespace

bool completeEnum(const OptionValueContext &Context, const OptionType &Type,
                  const std::string &Prefix, llvm::StringRef Src,
                  std::set<std::string> &SeenLabels,
                  std::vector<CompletionItem> &Items) {
  if (Type.EnumValues.empty())
    return false;

  for (const OptionType::EnumValue &Value : Type.EnumValues) {
    std::optional<EnumValue> Completion = completionValue(Value);
    if (!Completion || !Completion->FilterText.starts_with(Prefix) ||
        SeenLabels.contains(Completion->Label))
      continue;

    CompletionItem Item{
        .label = Completion->Label,
        .kind = CompletionItemKind::EnumMember,
        .detail = "enum option value",
        .filterText = Completion->FilterText,
    };
    addInsertEdits(Context, Src, Completion->Label, Item);
    SeenLabels.insert(Completion->Label);
    addItem(Items, std::move(Item));
  }

  return true;
}

bool completeBoolean(const OptionValueContext &Context, const OptionType &Type,
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
    addInsertEdits(Context, Src, Value, Item);
    SeenLabels.insert(std::string(Value));
    addItem(Items, std::move(Item));
  }
  return true;
}

bool completeNull(const OptionValueContext &Context, const std::string &Prefix,
                  llvm::StringRef Src, std::set<std::string> &SeenLabels,
                  std::vector<CompletionItem> &Items) {
  if (!std::string_view("null").starts_with(Prefix) ||
      SeenLabels.contains("null"))
    return false;

  CompletionItem Item{
      .label = "null",
      .kind = CompletionItemKind::Keyword,
      .detail = "null option value",
  };
  addInsertEdits(Context, Src, "null", Item);
  SeenLabels.insert("null");
  addItem(Items, std::move(Item));
  return true;
}

} // namespace nixd::completion::options::value

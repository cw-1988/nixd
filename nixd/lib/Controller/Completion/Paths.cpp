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

struct PathInfo {
  bool Accepts = false;
  bool Absolute = false;
  bool InStore = false;
};

PathInfo pathInfoFor(const OptionType &Type) {
  const std::string LowerName = option_navigation::lowerTypeName(Type);
  PathInfo Info;
  if (Type.Path) {
    Info.Accepts = true;
    Info.Absolute = Type.Path->Absolute.value_or(false) ||
                    Type.Path->InStore.value_or(false);
    Info.InStore = Type.Path->InStore.value_or(false);
    return Info;
  }
  if (LowerName == "path" || LowerName == "pathinstore" ||
      LowerName == "absolutepath") {
    Info.Accepts = true;
    Info.Absolute = LowerName == "pathinstore" || LowerName == "absolutepath";
    Info.InStore = LowerName == "pathinstore";
    return Info;
  }
  if (Type.Description) {
    const std::string Lower = lowerCopy(*Type.Description);
    if (Lower == "path") {
      Info.Accepts = true;
      return Info;
    }
    if (Lower == "absolute path" || Lower == "store path") {
      Info.Accepts = true;
      Info.Absolute = true;
      Info.InStore = Lower == "store path";
      return Info;
    }
  }
  for (const OptionType &Alternative : alternativeTypesFor(Type, LowerName)) {
    PathInfo AlternativeInfo = pathInfoFor(Alternative);
    Info.Accepts |= AlternativeInfo.Accepts;
    Info.Absolute |= AlternativeInfo.Absolute;
    Info.InStore |= AlternativeInfo.InStore;
  }
  if (Info.Accepts)
    return Info;
  if (LowerName == "nullor" || LowerName == "unique") {
    if (std::optional<OptionType> Elem = elemTypeFor(Type))
      return pathInfoFor(*Elem);
  }
  return Info;
}

} // namespace

bool typeAcceptsPath(const OptionType &Type) {
  return pathInfoFor(Type).Accepts;
}

bool completePath(const OptionValueContext &Context, const OptionType &Type,
                  const std::string &Prefix, llvm::StringRef Src,
                  std::set<std::string> &SeenLabels,
                  std::vector<CompletionItem> &Items) {
  const PathInfo Info = pathInfoFor(Type);
  if (!Info.Accepts)
    return false;

  std::vector<std::string_view> Values;
  if (Info.InStore)
    Values = {"/nix/store/"};
  else if (Info.Absolute)
    Values = {"/"};
  else
    Values = {"./"};

  bool Added = false;
  for (std::string_view Value : Values) {
    if (!Value.starts_with(Prefix) || SeenLabels.contains(std::string(Value)))
      continue;
    CompletionItem Item{
        .label = std::string(Value),
        .kind = Value == "./" ? CompletionItemKind::Folder
                              : CompletionItemKind::File,
        .detail = "path option value",
    };
    addInsertEdits(Context, Src, Value, Item);
    SeenLabels.insert(std::string(Value));
    addItem(Items, std::move(Item));
    Added = true;
  }
  return Added;
}

} // namespace nixd::completion::options::value

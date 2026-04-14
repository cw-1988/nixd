#include "Controller/Completion/Value.h"

#include "nixd/Eval/AttrSetClient.h"

#include <llvm/Support/Error.h>

#include <memory>
#include <optional>
#include <semaphore>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace nixd;
using namespace lspserver;

namespace nixd::completion::options::value {
namespace {

struct ReplyState {
  std::binary_semaphore Ready{0};
  std::vector<std::string> Names;
};

} // namespace

bool typeAcceptsPackage(const OptionType &Type) {
  const std::string LowerName = Type.Name ? lowerCopy(*Type.Name) : "";
  if (LowerName == "package")
    return true;
  if (Type.Description &&
      lowerCopy(*Type.Description).find("package") != std::string::npos)
    return true;
  for (const OptionType &Alternative : alternativeTypesFor(Type, LowerName))
    if (typeAcceptsPackage(Alternative))
      return true;
  if (LowerName == "nullor" || LowerName == "unique") {
    if (std::optional<OptionType> Elem = elemTypeFor(Type))
      return typeAcceptsPackage(*Elem);
  }
  return false;
}

bool completePackage(const OptionValueContext &Context, const OptionType &Type,
                     const std::string &Prefix, llvm::StringRef Src,
                     AttrSetClient *NixpkgsClient,
                     std::set<std::string> &SeenLabels,
                     std::vector<CompletionItem> &Items) {
  if (!NixpkgsClient || !typeAcceptsPackage(Type) || isSelectContext(Context) ||
      Prefix.starts_with("pkgs."))
    return false;

  auto State = std::make_shared<ReplyState>();
  auto OnReply = [State](llvm::Expected<AttrPathCompleteResponse> Resp) {
    if (Resp)
      State->Names = *Resp;
    else
      llvm::consumeError(Resp.takeError());
    State->Ready.release();
  };
  NixpkgsClient->attrpathComplete(
      AttrPathCompleteParams{.Scope = {}, .Prefix = Prefix},
      std::move(OnReply));
  State->Ready.acquire();

  bool Added = false;
  for (const std::string &Name : State->Names) {
    if (!Name.starts_with(Prefix))
      continue;
    const std::string Label = "pkgs." + Name;
    if (SeenLabels.contains(Label))
      continue;
    CompletionItem Item{
        .label = Label,
        .kind = CompletionItemKind::Field,
        .detail = "package option value",
        .filterText = Name,
        .insertText = Label,
    };
    addInsertEdits(Context, Src, Label, Item);
    SeenLabels.insert(Label);
    addItem(Items, std::move(Item));
    Added = true;
  }
  return Added;
}

} // namespace nixd::completion::options::value

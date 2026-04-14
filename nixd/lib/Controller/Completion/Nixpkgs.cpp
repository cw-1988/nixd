#include "Controller/Completion/Nixpkgs.h"

#include "Controller/Completion/Options.h"
#include "lspserver/Logger.h"
#include "nixd/Eval/AttrSetClient.h"

#include <llvm/Support/Error.h>
#include <llvm/Support/FormatVariadic.h>

#include <semaphore>
#include <utility>

using namespace lspserver;

nixd::completion::NixpkgsCompletionProvider::NixpkgsCompletionProvider(
    AttrSetClient &NixpkgsClient)
    : NixpkgsClient(NixpkgsClient) {}

void nixd::completion::NixpkgsCompletionProvider::resolvePackage(
    std::vector<std::string> Scope, std::string Name, CompletionItem &Item) {
  std::binary_semaphore Ready(0);
  AttrPathInfoResponse Desc;
  auto OnReply = [&Ready, &Desc](llvm::Expected<AttrPathInfoResponse> Resp) {
    if (Resp)
      Desc = *Resp;
    Ready.release();
  };
  Scope.emplace_back(std::move(Name));
  NixpkgsClient.attrpathInfo(Scope, std::move(OnReply));
  Ready.acquire();

  const PackageDescription &PD = Desc.PackageDesc;
  Item.documentation = MarkupContent{
      .kind = MarkupKind::Markdown,
      .value = PD.Description.value_or("") + "\n\n" +
               PD.LongDescription.value_or(""),
  };
  Item.detail = PD.Version.value_or("?");
}

void nixd::completion::NixpkgsCompletionProvider::completePackages(
    const AttrPathCompleteParams &Params, std::vector<CompletionItem> &Items) {
  std::binary_semaphore Ready(0);
  std::vector<std::string> Names;
  auto OnReply = [&Ready,
                  &Names](llvm::Expected<AttrPathCompleteResponse> Resp) {
    if (!Resp) {
      lspserver::elog("nixpkgs evaluator reported: {0}", Resp.takeError());
      Ready.release();
      return;
    }
    Names = *Resp;
    Ready.release();
  };

  NixpkgsClient.attrpathComplete(Params, std::move(OnReply));
  Ready.acquire();

  for (const auto &Name : Names) {
    if (Name.starts_with(Params.Prefix)) {
      addItem(Items, CompletionItem{
                         .label = Name,
                         .kind = CompletionItemKind::Field,
                         .data = llvm::formatv("{0}", toJSON(Params)),
                     });
    }
  }
}

nixd::AttrPathCompleteParams
nixd::completion::attrPathCompleteParams(Selector Sel, bool IsComplete) {
  if (IsComplete || Sel.empty()) {
    return {
        .Scope = std::move(Sel),
        .Prefix = "",
    };
  }
  std::string Back = std::move(Sel.back());
  Sel.pop_back();
  return {
      .Scope = Sel,
      .Prefix = std::move(Back),
  };
}

#pragma once

#include "nixd/Controller/Option.h"
#include "nixd/Protocol/AttrSet.h"

#include "lspserver/Protocol.h"

#include <llvm/ADT/StringRef.h>

#include <exception>
#include <optional>
#include <utility>
#include <vector>

namespace nixd {
class AttrSetClient;
}

namespace nixd::completion {

/// Set max completion size to this value, we don't want to send large lists
/// because of slow IO.
/// Items exceed this size should be marked "incomplete" and recomputed.
inline constexpr int MaxCompletionSize = 30;

struct ExceedSizeError : std::exception {
  [[nodiscard]] const char *what() const noexcept override {
    return "Size exceeded";
  }
};

inline void addItem(std::vector<lspserver::CompletionItem> &Items,
                    lspserver::CompletionItem Item) {
  if (Items.size() >= MaxCompletionSize)
    throw ExceedSizeError();
  Items.emplace_back(std::move(Item));
}

std::optional<AttrPathCompleteParams>
optionAttrPathCompletionParams(const nixf::Node &N,
                               const nixf::ParentMapAnalysis &PM);

void completeOptionNames(const std::vector<ResolvedOptionField> &Fields,
                         bool CompletionSnippets,
                         std::vector<lspserver::CompletionItem> &List);

void completeOptionValue(const OptionValueContext &Context,
                         const std::vector<ResolvedOptionInfo> &Infos,
                         bool CompletionSnippets,
                         AttrSetClient *NixpkgsClient, llvm::StringRef Src,
                         std::vector<lspserver::CompletionItem> &Items);

} // namespace nixd::completion

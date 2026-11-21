#pragma once

#include "lspserver/Protocol.h"

#include <llvm/ADT/StringRef.h>

#include <vector>

namespace nixd::completion {

/// \brief Complete the attributes Nix accepts at the root of `flake.nix`.
///
/// Offers only the names matching \p Prefix. This is a closed schema, so the
/// caller must not fall back to any other name provider afterwards.
void completeFlakeRootNames(llvm::StringRef Prefix, bool CompletionSnippets,
                            std::vector<lspserver::CompletionItem> &Items);

} // namespace nixd::completion

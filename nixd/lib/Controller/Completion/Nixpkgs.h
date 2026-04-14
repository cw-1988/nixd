#pragma once

#include "nixd/Protocol/AttrSet.h"

#include "lspserver/Protocol.h"

#include <string>
#include <vector>

namespace nixd {
class AttrSetClient;
}

namespace nixd::completion {

class NixpkgsCompletionProvider {
  AttrSetClient &NixpkgsClient;

public:
  NixpkgsCompletionProvider(AttrSetClient &NixpkgsClient);

  void resolvePackage(std::vector<std::string> Scope, std::string Name,
                      lspserver::CompletionItem &Item);

  void completePackages(const AttrPathCompleteParams &Params,
                        std::vector<lspserver::CompletionItem> &Items);
};

AttrPathCompleteParams attrPathCompleteParams(Selector Sel, bool IsComplete);

} // namespace nixd::completion

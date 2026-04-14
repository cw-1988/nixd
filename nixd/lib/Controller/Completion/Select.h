#pragma once

#include "lspserver/Protocol.h"

#include <vector>

namespace nixd {
class AttrSetClient;
}

namespace nixf {
class ExprSelect;
class ParentMapAnalysis;
class VariableLookupAnalysis;
} // namespace nixf

namespace nixd::completion {

void completeSelect(const nixf::ExprSelect &Select, AttrSetClient &Client,
                    const nixf::VariableLookupAnalysis &VLA,
                    const nixf::ParentMapAnalysis &PM, bool IsComplete,
                    std::vector<lspserver::CompletionItem> &List);

} // namespace nixd::completion

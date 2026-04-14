#pragma once

#include "lspserver/Protocol.h"

#include <vector>

namespace nixd {
class AttrSetClient;
}

namespace nixf {
class ExprVar;
class ParentMapAnalysis;
class VariableLookupAnalysis;
} // namespace nixf

namespace nixd::completion {

void completeVarName(const nixf::VariableLookupAnalysis &VLA,
                     const nixf::ParentMapAnalysis &PM, const nixf::ExprVar &N,
                     AttrSetClient &Client,
                     std::vector<lspserver::CompletionItem> &List);

} // namespace nixd::completion

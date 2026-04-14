#pragma once

#include <nixf/Basic/Nodes/Basic.h>
#include <nixf/Basic/Range.h>

#include <string_view>

namespace nixd::completion {

const nixf::Node *findCompletionNode(const nixf::Node &AST,
                                     std::string_view Src, nixf::Position Pos);

} // namespace nixd::completion

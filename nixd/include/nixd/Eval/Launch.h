#pragma once

#include "AttrSetClient.h"

#include <memory>

namespace nixd {

void startAttrSetEval(const std::string &Name,
                      std::shared_ptr<AttrSetClientProc> &Worker);

void startNixpkgs(std::shared_ptr<AttrSetClientProc> &NixpkgsEval);

void startOption(const std::string &Name,
                 std::shared_ptr<AttrSetClientProc> &Worker);

} // namespace nixd

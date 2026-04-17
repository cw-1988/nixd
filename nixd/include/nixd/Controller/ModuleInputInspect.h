#pragma once

#include "nixd/Controller/Option.h"

#include <llvm/Support/JSON.h>

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nixf {
class Node;
class ParentMapAnalysis;
class VariableLookupAnalysis;
} // namespace nixf

namespace nixd {

struct ModuleInputInspectContext {
  std::string Input;
  std::vector<std::string> Scope;
  std::vector<std::string> Sources;
};

using ModuleInputInfoResolver =
    std::function<std::vector<ResolvedOptionInfo>(
        const std::vector<std::string> &)>;

using ModuleInputOptionCompleter =
    std::function<std::vector<ResolvedOptionField>(
        const std::vector<std::string> &, const std::string &)>;

using ModuleInputAttrCompleter =
    std::function<std::vector<std::string>(const std::vector<std::string> &,
                                           const std::string &)>;

std::optional<ModuleInputInspectContext>
findModuleInputInspectContext(const nixf::Node &N,
                              const nixf::VariableLookupAnalysis &VLA,
                              const nixf::ParentMapAnalysis &PM,
                              const ModuleInputInfoResolver &Resolve);

llvm::json::Array
moduleInputInspectScopeToJSON(const std::vector<std::string> &Scope);

llvm::json::Array
moduleInputInspectSourcesToJSON(const std::vector<std::string> &Sources);

std::optional<std::vector<std::string>>
moduleInputInspectScopeFromJSON(const llvm::json::Value &V);

std::string renderModuleInputInspectionDocument(
    std::string_view Input, const std::vector<std::string> &Scope,
    const std::vector<std::string> &Sources, std::string_view SourceFile,
    const ModuleInputOptionCompleter &CompleteOptions,
    const ModuleInputAttrCompleter &CompleteAttrs = {});

std::filesystem::path
writeModuleInputInspectionFile(std::string_view Input,
                               std::string_view SourceFile,
                               std::string Content);

} // namespace nixd

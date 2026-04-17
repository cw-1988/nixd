#pragma once

#include <filesystem>
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

struct FlakeOutputInput {
  std::string Name;
  std::optional<std::string> URL;
  bool IsSelf = false;
};

struct FlakeOutputInputContext {
  enum class Kind {
    Named,
    Ellipsis,
  };

  Kind InputKind = Kind::Named;
  std::string Name;
  const nixf::Node *RangeNode = nullptr;
  std::vector<FlakeOutputInput> Inputs;
};

std::optional<FlakeOutputInputContext>
findFlakeOutputInputContext(const nixf::Node &N,
                            const nixf::VariableLookupAnalysis &VLA,
                            const nixf::ParentMapAnalysis &PM,
                            std::string_view File);

std::vector<FlakeOutputInput>
collectFlakeOutputInputs(const nixf::Node &N,
                         const nixf::ParentMapAnalysis &PM);

std::string renderFlakeOutputInputMarkdown(std::string_view Name,
                                           const FlakeOutputInput &Input);

std::string renderFlakeOutputEllipsisMarkdown(
    const std::vector<FlakeOutputInput> &Inputs);

std::string renderFlakeOutputInputInspectionDocument(
    const FlakeOutputInputContext &Context, std::string_view SourceFile);

std::filesystem::path
writeFlakeOutputInputInspectionFile(const FlakeOutputInputContext &Context,
                                    std::string_view SourceFile,
                                    std::string Content);

} // namespace nixd

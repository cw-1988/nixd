#pragma once

#include "nixd/Protocol/AttrSet.h"

#include "nixf/Basic/Nodes/Expr.h"
#include "nixf/Sema/ParentMap.h"

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace nixd {

class AttrSetClient;

enum class OptionLiteralKind : std::uint8_t {
  Unknown,
  Null,
  Bool,
  Int,
  Float,
  String,
  Path,
  List,
  AttrSet,
};

enum class OptionTypeCoverage : std::uint8_t {
  Unknown,
  Partial,
  Complete,
};

enum class OptionValueMatch : std::uint8_t {
  Matches,
  Mismatches,
  Unknown,
};

struct ParsedOptionType {
  OptionTypeCoverage Coverage = OptionTypeCoverage::Unknown;
  bool AllowNull = false;
  bool AcceptsAbsolutePathString = false;
  std::set<OptionLiteralKind> Accepted;
  std::string Rendered;

  [[nodiscard]] bool accepts(OptionLiteralKind Kind) const;
  [[nodiscard]] bool acceptsBoolean() const;
};

struct OptionValueContext {
  const nixf::Binding *Binding = nullptr;
  std::vector<std::string> Scope;
};

struct OptionProviderRef {
  std::string Name;
  AttrSetClient *Client = nullptr;
  std::uint64_t Generation = 0;
};

struct ResolvedOptionInfo {
  std::string ProviderName;
  OptionDescription Description;
};

struct ResolvedOptionField {
  std::string ProviderName;
  OptionField Field;
};

/// OptionType: conservative interpretation of option metadata and Nix literals.
std::optional<ParsedOptionType> parseOptionType(const OptionType &Type);
OptionLiteralKind classifyOptionLiteral(const nixf::Expr &Expr);
OptionValueMatch optionValueMatch(const ParsedOptionType &Expected,
                                  const nixf::Expr &Value,
                                  OptionLiteralKind Actual);
std::string optionLiteralKindName(OptionLiteralKind Kind);

/// OptionContext: AST helpers for option paths and option value positions.
std::optional<OptionValueContext>
findOptionValueContext(const nixf::Node &Node,
                       const nixf::ParentMapAnalysis &PM, nixf::Position Pos);

std::optional<std::vector<std::string>>
findOptionBindingScope(const nixf::Binding &Binding,
                       const nixf::ParentMapAnalysis &PM);

/// OptionService: shared provider IPC and option-info cache for LSP features.
class OptionService {
  struct InfoCacheKey {
    std::string ProviderName;
    std::uint64_t Generation = 0;
    std::vector<std::string> Scope;

    bool operator<(const InfoCacheKey &Other) const;
  };

  std::mutex CacheLock;
  std::map<InfoCacheKey, std::optional<OptionDescription>> InfoCache;

  std::optional<OptionDescription>
  resolveProviderInfo(const OptionProviderRef &Provider,
                      const std::vector<std::string> &Scope);

public:
  void invalidate();
  void invalidateProvider(std::string_view ProviderName);

  std::vector<ResolvedOptionField>
  complete(const std::vector<OptionProviderRef> &Providers,
           const std::vector<std::string> &Scope, const std::string &Prefix);

  std::vector<ResolvedOptionInfo>
  resolve(const std::vector<OptionProviderRef> &Providers,
          const std::vector<std::string> &Scope);

  std::vector<lspserver::Location>
  declarationLocations(const std::vector<OptionProviderRef> &Providers,
                       const std::vector<std::string> &Scope);
};

} // namespace nixd

#include "OptionDiagnosticsSupport.h"
#include "OptionTypeValidation.h"
#include "nixd/Controller/Controller.h"
#include "nixd/Controller/Option.h"

#include <algorithm>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <tuple>

#include <nixf/Basic/Nodes/Attrs.h>

using namespace nixd;
using namespace nixd::option_diagnostics;
using namespace nixf;

namespace {

constexpr size_t MaxOptionDiagnosticQueries = 128;

struct OptionDiagnosticContext {
  std::map<std::vector<std::string>, std::vector<ResolvedOptionInfo>> InfoCache;
  size_t Queries = 0;
};

std::vector<NixdDiagnostic>
validateOptionBinding(const Binding &Binding, const ParentMapAnalysis &PM,
                      const VariableLookupAnalysis *VLA,
                      OptionDiagnosticContext &Context,
                      const std::function<std::vector<ResolvedOptionInfo>(
                          const std::vector<std::string> &)> &Resolve) {
  const auto &Value = Binding.value();
  if (!Value)
    return {};
  if (isLetDefinitionBinding(Binding, PM))
    return {};

  std::optional<std::vector<std::string>> Scope =
      findOptionBindingScope(Binding, PM);
  if (!Scope)
    return {};

  auto [It, Inserted] = Context.InfoCache.try_emplace(*Scope);
  if (Inserted) {
    if (Context.Queries >= MaxOptionDiagnosticQueries)
      return {};
    ++Context.Queries;
    It->second = Resolve(*Scope);
  }

  std::optional<std::vector<NixdDiagnostic>> FirstMismatch;
  for (const ResolvedOptionInfo &Info : It->second) {
    if (!Info.Description.Type)
      continue;

    ValidationResult Result =
        validateType(*Info.Description.Type, *Value, *Scope, PM, VLA);
    if (Result.Match == SchemaMatch::Matches)
      return {};
    if (Result.Match == SchemaMatch::Unknown)
      continue;

    if (!FirstMismatch)
      FirstMismatch = std::move(Result.Diagnostics);
  }
  return FirstMismatch ? std::move(*FirstMismatch)
                       : std::vector<NixdDiagnostic>{};
}

void collectOptionDiagnosticsFromNode(
    const Node &Desc, const ParentMapAnalysis &PM,
    const VariableLookupAnalysis *VLA, OptionDiagnosticContext &Context,
    const std::function<std::vector<ResolvedOptionInfo>(
        const std::vector<std::string> &)> &Resolve,
    std::vector<NixdDiagnostic> &Diagnostics) {
  if (Desc.kind() == Node::NK_Binding) {
    std::vector<NixdDiagnostic> NewDiagnostics = validateOptionBinding(
        static_cast<const Binding &>(Desc), PM, VLA, Context, Resolve);
    std::move(NewDiagnostics.begin(), NewDiagnostics.end(),
              std::back_inserter(Diagnostics));
  }

  for (const nixf::Node *Child : Desc.children()) {
    if (Child)
      collectOptionDiagnosticsFromNode(*Child, PM, VLA, Context, Resolve,
                                       Diagnostics);
  }
}

std::vector<NixdDiagnostic> dedupeDiagnostics(std::vector<NixdDiagnostic> In) {
  std::vector<NixdDiagnostic> Out;
  std::set<std::tuple<size_t, size_t, std::string, std::string>> Seen;
  for (NixdDiagnostic &Diagnostic : In) {
    auto Key = std::make_tuple(Diagnostic.Range.lCur().offset(),
                               Diagnostic.Range.rCur().offset(),
                               Diagnostic.Code, Diagnostic.Message);
    if (Seen.insert(std::move(Key)).second)
      Out.emplace_back(std::move(Diagnostic));
  }
  return Out;
}

} // namespace

std::vector<NixdDiagnostic>
Controller::collectOptionDiagnostics(const NixTU &TU) {
  std::vector<NixdDiagnostic> Diagnostics;
  if (!TU.ast() || !TU.parentMap())
    return Diagnostics;

  OptionDiagnosticContext Context;
  auto Resolve = [this](const std::vector<std::string> &Scope) {
    return resolveOptionInfos(Scope);
  };
  collectOptionDiagnosticsFromNode(*TU.ast(), *TU.parentMap(),
                                   TU.variableLookup(), Context, Resolve,
                                   Diagnostics);
  return dedupeDiagnostics(std::move(Diagnostics));
}

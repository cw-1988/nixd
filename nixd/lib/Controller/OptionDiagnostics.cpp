#include "OptionDiagnosticsSupport.h"
#include "OptionTypeValidation.h"
#include "OptionTypeNavigation.h"
#include "nixd/Controller/Controller.h"
#include "nixd/Controller/Option.h"

#include <algorithm>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <tuple>
#include <unordered_set>

#include <nixf/Basic/Nodes/Attrs.h>

using namespace nixd;
using namespace nixd::option_diagnostics;
using namespace nixf;

namespace {

constexpr size_t MaxOptionDiagnosticQueries = 128;

struct OptionDiagnosticContext {
  struct FieldCacheKey {
    std::vector<std::string> Scope;
    std::string Prefix;

    bool operator<(const FieldCacheKey &Other) const {
      return std::tie(Scope, Prefix) < std::tie(Other.Scope, Other.Prefix);
    }
  };

  std::map<std::vector<std::string>, std::vector<ResolvedOptionInfo>> InfoCache;
  std::map<FieldCacheKey, std::vector<ResolvedOptionField>> FieldCache;
  size_t Queries = 0;
  bool QueryLimitReached = false;
};

std::vector<ResolvedOptionInfo> resolveCached(
    OptionDiagnosticContext &Context, const std::vector<std::string> &Scope,
    const std::function<std::vector<ResolvedOptionInfo>(
        const std::vector<std::string> &)> &Resolve) {
  auto [It, Inserted] = Context.InfoCache.try_emplace(Scope);
  if (Inserted) {
    if (Context.Queries >= MaxOptionDiagnosticQueries) {
      Context.QueryLimitReached = true;
      return {};
    }
    ++Context.Queries;
    It->second = Resolve(Scope);
  }
  return It->second;
}

std::vector<ResolvedOptionField> completeCached(
    OptionDiagnosticContext &Context, const std::vector<std::string> &Scope,
    const std::string &Prefix,
    const std::function<std::vector<ResolvedOptionField>(
        const std::vector<std::string> &, const std::string &)> &Complete) {
  OptionDiagnosticContext::FieldCacheKey Key{.Scope = Scope, .Prefix = Prefix};
  auto [It, Inserted] = Context.FieldCache.try_emplace(Key);
  if (Inserted) {
    if (Context.Queries >= MaxOptionDiagnosticQueries) {
      Context.QueryLimitReached = true;
      return {};
    }
    ++Context.Queries;
    It->second = Complete(Scope, Prefix);
  }
  return It->second;
}

bool hasUnsafeAncestorAttrs(const Binding &Binding,
                            const ParentMapAnalysis &PM) {
  const Node *Current = &Binding;
  std::unordered_set<const Node *> Seen;
  while (Current && Seen.insert(Current).second) {
    const Node *Parent = PM.query(*Current);
    if (!Parent)
      return false;
    if (Parent->kind() == Node::NK_ExprAttrs) {
      const auto &Attrs = static_cast<const ExprAttrs &>(*Parent);
      if (!Attrs.sema().dynamicAttrs().empty() || hasInheritBinding(Attrs))
        return true;
    }
    Current = Parent;
  }
  return false;
}

bool isNestedInList(const Binding &Binding, const ParentMapAnalysis &PM) {
  const Node *Current = &Binding;
  std::unordered_set<const Node *> Seen;
  while (Current && Seen.insert(Current).second) {
    const Node *Parent = PM.query(*Current);
    if (!Parent)
      return false;
    if (Parent->kind() == Node::NK_ExprList)
      return true;
    if (Parent->kind() == Node::NK_Binding)
      return false;
    Current = Parent;
  }
  return false;
}

std::optional<NixdDiagnostic> validateKnownOptionPath(
    const Binding &Binding, const ParentMapAnalysis &PM,
    const std::vector<std::string> &Scope, OptionDiagnosticContext &Context,
    const std::function<std::vector<ResolvedOptionInfo>(
        const std::vector<std::string> &)> &Resolve,
    const std::function<std::vector<ResolvedOptionField>(
        const std::vector<std::string> &, const std::string &)> &Complete) {
  if (Scope.empty() || Scope.back() == "imports" ||
      hasUnsafeAncestorAttrs(Binding, PM))
    return std::nullopt;

  std::vector<std::string> ParentScope(Scope.begin(), Scope.end() - 1);
  const std::string &Leaf = Scope.back();

  for (const ResolvedOptionInfo &Info :
       resolveCached(Context, ParentScope, Resolve)) {
    if (!Info.Description.Type)
      continue;
    const OptionType &Type = *Info.Description.Type;
    if (!Type.KnownSubOptionsComplete ||
        option_navigation::hasDynamicAttrCoverage(Type))
      return std::nullopt;
  }
  if (Context.QueryLimitReached)
    return std::nullopt;

  std::vector<ResolvedOptionField> ExactFields =
      completeCached(Context, ParentScope, Leaf, Complete);
  if (Context.QueryLimitReached)
    return std::nullopt;
  for (const ResolvedOptionField &Field : ExactFields)
    if (Field.Field.Name == Leaf)
      return std::nullopt;

  std::vector<ResolvedOptionField> SiblingFields =
      completeCached(Context, ParentScope, "", Complete);
  if (Context.QueryLimitReached)
    return std::nullopt;
  if (SiblingFields.empty())
    return std::nullopt;
  for (const ResolvedOptionField &Field : SiblingFields)
    if (Field.Field.Name == Leaf)
      return std::nullopt;

  return makeUnknownDiagnostic(Binding.path(), Scope);
}

std::vector<NixdDiagnostic>
validateOptionBinding(const Binding &Binding, const ParentMapAnalysis &PM,
                      const VariableLookupAnalysis *VLA,
                      OptionDiagnosticContext &Context,
                      const std::function<std::vector<ResolvedOptionInfo>(
                          const std::vector<std::string> &)> &Resolve,
                      const std::function<std::vector<ResolvedOptionField>(
                          const std::vector<std::string> &,
                          const std::string &)> &Complete) {
  const auto &Value = Binding.value();
  if (!Value)
    return {};
  if (isLetDefinitionBinding(Binding, PM))
    return {};
  if (isNestedInList(Binding, PM))
    return {};

  std::optional<std::vector<std::string>> Scope =
      findOptionBindingScope(Binding, PM);
  if (!Scope)
    return {};

  std::vector<ResolvedOptionInfo> Infos =
      resolveCached(Context, *Scope, Resolve);

  if (Infos.empty()) {
    if (std::optional<NixdDiagnostic> Unknown = validateKnownOptionPath(
            Binding, PM, *Scope, Context, Resolve, Complete))
      return {*Unknown};
    return {};
  }

  std::optional<std::vector<NixdDiagnostic>> FirstMismatch;
  for (const ResolvedOptionInfo &Info : Infos) {
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
    const std::function<std::vector<ResolvedOptionField>(
        const std::vector<std::string> &, const std::string &)> &Complete,
    std::unordered_set<const Node *> &Seen,
    std::vector<NixdDiagnostic> &Diagnostics) {
  if (!Seen.insert(&Desc).second)
    return;

  if (Desc.kind() == Node::NK_Binding) {
    std::vector<NixdDiagnostic> NewDiagnostics = validateOptionBinding(
        static_cast<const Binding &>(Desc), PM, VLA, Context, Resolve,
        Complete);
    std::move(NewDiagnostics.begin(), NewDiagnostics.end(),
              std::back_inserter(Diagnostics));
  }

  for (const nixf::Node *Child : Desc.children()) {
    if (Child)
      collectOptionDiagnosticsFromNode(*Child, PM, VLA, Context, Resolve,
                                       Complete, Seen,
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

bool hasRecoverySyntaxError(const NixTU &TU) {
  for (const nixf::Diagnostic &Diagnostic : TU.diagnostics()) {
    const nixf::Diagnostic::Severity Severity =
        nixf::Diagnostic::severity(Diagnostic.kind());
    if (Severity == nixf::Diagnostic::DS_Fatal)
      return true;

    const std::string_view Name = nixf::Diagnostic::sname(Diagnostic.kind());
    if (Severity <= nixf::Diagnostic::DS_Error &&
        (Name.starts_with("parse-") || Name.starts_with("lex-")))
      return true;
  }
  return false;
}

} // namespace

std::vector<NixdDiagnostic>
Controller::collectOptionDiagnostics(const NixTU &TU) {
  std::vector<NixdDiagnostic> Diagnostics;
  if (!TU.ast() || !TU.parentMap())
    return Diagnostics;
  if (hasRecoverySyntaxError(TU))
    return Diagnostics;
  if (!waitForOptionProvidersReadyForTests())
    return Diagnostics;
  if (!optionProvidersReadyForDiagnostics())
    return Diagnostics;

  OptionDiagnosticContext Context;
  auto Resolve = [this](const std::vector<std::string> &Scope) {
    return resolveDerivedOptionInfos(Scope);
  };
  auto Complete = [this](const std::vector<std::string> &Scope,
                         const std::string &Prefix) {
    return completeDerivedOptions(Scope, Prefix);
  };
  std::unordered_set<const Node *> Seen;
  collectOptionDiagnosticsFromNode(*TU.ast(), *TU.parentMap(),
                                   TU.variableLookup(), Context, Resolve,
                                   Complete, Seen, Diagnostics);
  return dedupeDiagnostics(std::move(Diagnostics));
}

#include "DiagnosticsSupport.h"
#include "Navigation.h"
#include "Validation.h"
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

std::vector<ResolvedOptionInfo>
resolveCached(OptionDiagnosticContext &Context,
              const std::vector<std::string> &Scope,
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

bool bindingPathStartsWith(const Binding &Bind, std::string_view Name) {
  const auto &Names = Bind.path().names();
  if (Names.empty() || !Names.front() || !Names.front()->isStatic())
    return false;
  return Names.front()->staticName() == Name;
}

bool hasEnclosingBinding(const Binding &Bind, const ParentMapAnalysis &PM) {
  const Node *Current = &Bind;
  std::unordered_set<const Node *> Seen;
  while (Current && Seen.insert(Current).second) {
    const Node *Parent = PM.query(*Current);
    if (!Parent)
      return false;
    if (Parent->kind() == Node::NK_Binding)
      return true;
    Current = Parent;
  }
  return false;
}

bool isNestedInConfigWrapper(const Binding &Bind, const ParentMapAnalysis &PM) {
  const Node *Current = &Bind;
  std::unordered_set<const Node *> Seen;
  while (Current && Seen.insert(Current).second) {
    if (Current->kind() == Node::NK_Binding) {
      const auto &CurrentBinding = static_cast<const nixf::Binding &>(*Current);
      if (bindingPathStartsWith(CurrentBinding, "config") &&
          hasEnclosingBinding(CurrentBinding, PM))
        return true;
    }

    const Node *Parent = PM.query(*Current);
    if (!Parent)
      return false;
    Current = Parent;
  }
  return false;
}

bool isNestedInFunctionCall(const Binding &Binding,
                            const ParentMapAnalysis &PM) {
  const Node *Current = &Binding;
  std::unordered_set<const Node *> Seen;
  while (Current && Seen.insert(Current).second) {
    const Node *Parent = PM.query(*Current);
    if (!Parent)
      return false;
    if (Parent->kind() == Node::NK_ExprCall)
      return true;
    Current = Parent;
  }
  return false;
}

bool hasFunctionValue(const Binding &Binding) {
  const auto &Value = Binding.value();
  return Value && stripParens(*Value).kind() == Node::NK_ExprLambda;
}

std::optional<std::string> singleStaticBindingName(const Binding &Binding) {
  const auto &Names = Binding.path().names();
  if (Names.size() != 1 || !Names.front() || !Names.front()->isStatic())
    return std::nullopt;
  return Names.front()->staticName();
}

std::optional<std::string> firstStaticBindingName(const Binding &Binding) {
  const auto &Names = Binding.path().names();
  if (Names.empty() || !Names.front() || !Names.front()->isStatic())
    return std::nullopt;
  return Names.front()->staticName();
}

const ExprAttrs *parentAttrs(const Binding &Binding,
                             const ParentMapAnalysis &PM) {
  const Node *Binds = PM.query(Binding);
  if (!Binds || Binds->kind() != Node::NK_Binds)
    return nullptr;
  const Node *Attrs = PM.query(*Binds);
  if (!Attrs || Attrs->kind() != Node::NK_ExprAttrs)
    return nullptr;
  return static_cast<const ExprAttrs *>(Attrs);
}

bool isFlakeRootAttrs(const ExprAttrs &Attrs, const ParentMapAnalysis &PM) {
  if (!PM.isRoot(Attrs))
    return false;
  return Attrs.sema().staticAttrs().contains("outputs");
}

bool isFlakeRootBinding(const Binding &Binding, const ParentMapAnalysis &PM) {
  const ExprAttrs *Attrs = parentAttrs(Binding, PM);
  if (!Attrs || !isFlakeRootAttrs(*Attrs, PM))
    return false;

  std::optional<std::string> Name = firstStaticBindingName(Binding);
  if (!Name)
    return false;

  return *Name == "description" || *Name == "inputs" || *Name == "outputs" ||
         *Name == "nixConfig";
}

bool hasFunctionHelperExport(const ExprAttrs &Attrs) {
  for (const auto &[Name, Attr] : Attrs.sema().staticAttrs()) {
    if (!Name.starts_with("mk") || !Attr.value())
      continue;
    if (stripParens(*Attr.value()).kind() == Node::NK_ExprLambda)
      return true;
  }
  return false;
}

bool isKnownRootOptionField(
    std::string_view Name, OptionDiagnosticContext &Context,
    const std::function<std::vector<ResolvedOptionField>(
        const std::vector<std::string> &, const std::string &)> &Complete) {
  std::vector<ResolvedOptionField> Fields =
      completeCached(Context, {}, "", Complete);
  for (const ResolvedOptionField &Field : Fields)
    if (Field.Field.Name == Name)
      return true;
  return false;
}

bool isHelperLibraryExport(
    const Binding &Binding, const ParentMapAnalysis &PM,
    OptionDiagnosticContext &Context,
    const std::function<std::vector<ResolvedOptionField>(
        const std::vector<std::string> &, const std::string &)> &Complete) {
  std::optional<std::string> Name = singleStaticBindingName(Binding);
  if (!Name)
    return false;

  const Node *AttrsNode = PM.upTo(Binding, Node::NK_ExprAttrs);
  if (!AttrsNode)
    return false;

  const auto &Attrs = static_cast<const ExprAttrs &>(*AttrsNode);
  return hasFunctionHelperExport(Attrs) &&
         !isKnownRootOptionField(*Name, Context, Complete);
}

std::optional<std::vector<std::string>>
enclosingBindingScope(const Binding &Bind, const ParentMapAnalysis &PM) {
  const Node *Current = &Bind;
  std::unordered_set<const Node *> Seen;
  while (Current && Seen.insert(Current).second) {
    const Node *Parent = PM.query(*Current);
    if (!Parent)
      return std::nullopt;
    if (Parent->kind() == Node::NK_Binding)
      return findOptionBindingScope(static_cast<const nixf::Binding &>(*Parent),
                                    PM);
    Current = Parent;
  }
  return std::nullopt;
}

bool isProperPrefix(const std::vector<std::string> &Prefix,
                    const std::vector<std::string> &Scope) {
  return Prefix.size() < Scope.size() &&
         std::equal(Prefix.begin(), Prefix.end(), Scope.begin());
}

bool isListContainerType(const OptionType &Type) {
  const std::string LowerName = option_navigation::lowerTypeName(Type);
  if (LowerName == "listof" || LowerName == "loaof" ||
      option_navigation::isNonEmptyListType(Type))
    return true;
  if (LowerName == "nullor") {
    if (std::optional<OptionType> Elem = option_navigation::nullOrTypeFor(Type))
      return isListContainerType(*Elem);
  }
  if (LowerName == "unique") {
    if (std::optional<OptionType> Elem =
            option_navigation::elemTypeFor(Type, LowerName))
      return isListContainerType(*Elem);
  }
  for (const OptionType &Alternative :
       option_navigation::alternativeTypesFor(Type, LowerName)) {
    if (isListContainerType(Alternative))
      return true;
  }
  return false;
}

bool parentFieldHasDynamicAttrCoverage(
    OptionDiagnosticContext &Context, const std::vector<std::string> &Scope,
    const std::function<std::vector<ResolvedOptionField>(
        const std::vector<std::string> &, const std::string &)> &Complete) {
  if (Scope.empty())
    return false;

  std::vector<std::string> Parent(Scope.begin(), Scope.end() - 1);
  const std::string &Leaf = Scope.back();
  for (const ResolvedOptionField &Field :
       completeCached(Context, Parent, Leaf, Complete)) {
    if (Field.Field.Name != Leaf || !Field.Field.Description ||
        !Field.Field.Description->Type)
      continue;
    if (option_navigation::hasDynamicAttrCoverage(
            *Field.Field.Description->Type))
      return true;
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

  if (std::optional<std::vector<std::string>> Enclosing =
          enclosingBindingScope(Binding, PM)) {
    if (isProperPrefix(*Enclosing, Scope)) {
      for (const ResolvedOptionInfo &Info :
           resolveCached(Context, *Enclosing, Resolve)) {
        if (Info.Description.Type &&
            isListContainerType(*Info.Description.Type))
          return std::nullopt;
      }
    }
  }

  for (size_t PrefixLen = 1; PrefixLen < Scope.size(); ++PrefixLen) {
    std::vector<std::string> Prefix(Scope.begin(), Scope.begin() + PrefixLen);
    for (const ResolvedOptionInfo &Info :
         resolveCached(Context, Prefix, Resolve)) {
      if (Info.Description.Type &&
          option_navigation::hasDynamicAttrCoverage(*Info.Description.Type))
        return std::nullopt;
    }
    if (parentFieldHasDynamicAttrCoverage(Context, Prefix, Complete))
      return std::nullopt;
    if (Context.QueryLimitReached)
      return std::nullopt;
  }

  bool HasClosedParentType = false;
  for (const ResolvedOptionInfo &Info :
       resolveCached(Context, ParentScope, Resolve)) {
    if (!Info.Description.Type)
      continue;
    const OptionType &Type = *Info.Description.Type;
    if (!Type.KnownSubOptionsComplete ||
        option_navigation::hasDynamicAttrCoverage(Type))
      return std::nullopt;
    HasClosedParentType = true;
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
  if (SiblingFields.empty() && !HasClosedParentType)
    return std::nullopt;
  for (const ResolvedOptionField &Field : SiblingFields)
    if (Field.Field.Name == Leaf)
      return std::nullopt;

  return makeUnknownDiagnostic(Binding.path(), Scope);
}

std::vector<NixdDiagnostic> validateOptionBinding(
    const Binding &Binding, const ParentMapAnalysis &PM,
    const VariableLookupAnalysis *VLA, OptionDiagnosticContext &Context,
    const std::function<std::vector<ResolvedOptionInfo>(
        const std::vector<std::string> &)> &Resolve,
    const std::function<std::vector<ResolvedOptionField>(
        const std::vector<std::string> &, const std::string &)> &Complete) {
  const auto &Value = Binding.value();
  if (!Value)
    return {};
  if (isFlakeRootBinding(Binding, PM))
    return {};
  if (isLetDefinitionBinding(Binding, PM))
    return {};
  if (isNestedInList(Binding, PM))
    return {};
  if (isNestedInConfigWrapper(Binding, PM))
    return {};
  if (isNestedInFunctionCall(Binding, PM))
    return {};

  std::optional<std::vector<std::string>> Scope =
      findOptionBindingScope(Binding, PM);
  if (!Scope)
    return {};

  std::vector<ResolvedOptionInfo> Infos =
      resolveCached(Context, *Scope, Resolve);

  if (Infos.empty()) {
    if (hasFunctionValue(Binding) ||
        isHelperLibraryExport(Binding, PM, Context, Complete))
      return {};
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
    const auto &Binding = static_cast<const nixf::Binding &>(Desc);
    std::vector<NixdDiagnostic> NewDiagnostics =
        validateOptionBinding(Binding, PM, VLA, Context, Resolve, Complete);
    std::move(NewDiagnostics.begin(), NewDiagnostics.end(),
              std::back_inserter(Diagnostics));
    if (hasFunctionValue(Binding) ||
        isHelperLibraryExport(Binding, PM, Context, Complete))
      return;
  }

  for (const nixf::Node *Child : Desc.children()) {
    if (Child)
      collectOptionDiagnosticsFromNode(*Child, PM, VLA, Context, Resolve,
                                       Complete, Seen, Diagnostics);
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

#include "OptionTypeNavigation.h"

#include "OptionDiagnosticsSupport.h"

#include <iterator>
#include <utility>

using namespace nixd;

namespace {

std::string_view trimOuterParens(std::string_view S) {
  if (S.size() < 2 || S.front() != '(' || S.back() != ')')
    return S;
  return S.substr(1, S.size() - 2);
}

std::optional<OptionType> descriptionChild(const OptionType &Type,
                                           std::string_view Prefix) {
  if (!Type.Description)
    return std::nullopt;
  const std::string Lower =
      nixd::option_diagnostics::toLowerCopy(*Type.Description);
  std::string_view View = trimOuterParens(Lower);
  if (!View.starts_with(Prefix))
    return std::nullopt;

  OptionType Child;
  Child.Description = std::string(View.substr(Prefix.size()));
  return Child;
}

} // namespace

std::string nixd::option_navigation::lowerTypeName(const OptionType &Type) {
  return Type.Name ? option_diagnostics::toLowerCopy(*Type.Name) : "";
}

std::optional<OptionType>
nixd::option_navigation::nestedType(const OptionType &Type,
                                    std::string_view Name) {
  auto It = Type.NestedTypes.find(std::string(Name));
  if (It == Type.NestedTypes.end())
    return std::nullopt;
  return It->second;
}

std::optional<OptionType>
nixd::option_navigation::elemTypeFor(const OptionType &Type,
                                     std::string_view LowerName) {
  if (std::optional<OptionType> Elem = nestedType(Type, "elemType"))
    return Elem;
  if (LowerName == "listof" || LowerName == "loaof") {
    if (std::optional<OptionType> Elem = descriptionChild(Type, "list of "))
      return Elem;
    return descriptionChild(Type, "non-empty list of ");
  }
  if (LowerName == "attrsof" || LowerName == "lazyattrsof" ||
      LowerName == "attrswith")
    return descriptionChild(Type, "attribute set of ");
  if (LowerName == "unique")
    return descriptionChild(Type, "unique ");
  return std::nullopt;
}

std::optional<OptionType>
nixd::option_navigation::nullOrTypeFor(const OptionType &Type) {
  if (std::optional<OptionType> Elem = nestedType(Type, "elemType"))
    return Elem;
  return descriptionChild(Type, "null or ");
}

std::optional<OptionType>
nixd::option_navigation::functionResultTypeFor(const OptionType &Type) {
  if (std::optional<OptionType> Result = nestedType(Type, "resultType"))
    return Result;
  return nestedType(Type, "elemType");
}

std::vector<OptionType>
nixd::option_navigation::alternativeTypesFor(const OptionType &Type,
                                             std::string_view LowerName) {
  std::vector<OptionType> Alternatives;
  if (LowerName == "either" || LowerName == "oneof") {
    if (std::optional<OptionType> Left = nestedType(Type, "left"))
      Alternatives.emplace_back(std::move(*Left));
    if (std::optional<OptionType> Right = nestedType(Type, "right"))
      Alternatives.emplace_back(std::move(*Right));
    if (Alternatives.empty())
      for (const auto &Entry : Type.NestedTypes)
        Alternatives.emplace_back(Entry.second);
  } else if (LowerName == "coercedto") {
    if (std::optional<OptionType> Coerced = nestedType(Type, "coercedType"))
      Alternatives.emplace_back(std::move(*Coerced));
    if (std::optional<OptionType> Final = nestedType(Type, "finalType"))
      Alternatives.emplace_back(std::move(*Final));
  }
  return Alternatives;
}

bool nixd::option_navigation::hasSubOptionMetadata(const OptionType &Type) {
  return !Type.KnownSubOptions.empty() || !Type.KnownSubOptionsComplete;
}

bool nixd::option_navigation::isSubmoduleLike(const OptionType &Type) {
  const std::string LowerName = lowerTypeName(Type);
  return LowerName == "submodule" || LowerName == "submodulewith" ||
         hasSubOptionMetadata(Type);
}

bool nixd::option_navigation::isNonEmptyListType(const OptionType &Type) {
  const std::string LowerName = lowerTypeName(Type);
  if (LowerName.find("nonempty") != std::string::npos &&
      LowerName.find("list") != std::string::npos)
    return true;
  if (!Type.Description)
    return false;
  const std::string Lower =
      option_diagnostics::toLowerCopy(*Type.Description);
  std::string_view View = trimOuterParens(Lower);
  return View.starts_with("non-empty list of ") ||
         View.starts_with("non empty list of ");
}

bool nixd::option_navigation::hasFreeformCoverage(const OptionType &Type) {
  if (nestedType(Type, "freeformType"))
    return true;
  const std::string LowerName = lowerTypeName(Type);
  for (const OptionType &Alternative : alternativeTypesFor(Type, LowerName))
    if (hasFreeformCoverage(Alternative))
      return true;
  if ((LowerName == "nullor" || LowerName == "unique") ||
      LowerName == "coercedto") {
    if (std::optional<OptionType> Elem = elemTypeFor(Type, LowerName))
      return hasFreeformCoverage(*Elem);
  }
  return false;
}

bool nixd::option_navigation::hasDynamicAttrCoverage(const OptionType &Type) {
  const std::string LowerName = lowerTypeName(Type);
  if (LowerName == "attrsof" || LowerName == "lazyattrsof" ||
      LowerName == "attrswith")
    return true;
  if (hasFreeformCoverage(Type))
    return true;
  for (const OptionType &Alternative : alternativeTypesFor(Type, LowerName))
    if (hasDynamicAttrCoverage(Alternative))
      return true;
  if (LowerName == "nullor" || LowerName == "unique") {
    if (std::optional<OptionType> Elem = elemTypeFor(Type, LowerName))
      return hasDynamicAttrCoverage(*Elem);
  }
  return false;
}

std::optional<OptionType>
nixd::option_navigation::deriveTypeForSuffix(
    const OptionType &Type, const std::vector<std::string> &Suffix,
    size_t Index) {
  if (Index >= Suffix.size())
    return Type;

  const std::string LowerName = lowerTypeName(Type);

  if (LowerName == "nullor") {
    if (std::optional<OptionType> Elem = nullOrTypeFor(Type))
      return deriveTypeForSuffix(*Elem, Suffix, Index);
  }

  if (LowerName == "unique") {
    if (std::optional<OptionType> Elem = elemTypeFor(Type, LowerName))
      return deriveTypeForSuffix(*Elem, Suffix, Index);
  }

  for (const OptionType &Alternative : alternativeTypesFor(Type, LowerName)) {
    if (std::optional<OptionType> Derived =
            deriveTypeForSuffix(Alternative, Suffix, Index))
      return Derived;
  }

  if (LowerName == "attrsof" || LowerName == "lazyattrsof" ||
      LowerName == "attrswith" || LowerName == "loaof") {
    if (std::optional<OptionType> Elem = elemTypeFor(Type, LowerName))
      return deriveTypeForSuffix(*Elem, Suffix, Index + 1);
    return std::nullopt;
  }

  if (LowerName == "listof") {
    if (std::optional<OptionType> Elem = elemTypeFor(Type, LowerName))
      return deriveTypeForSuffix(*Elem, Suffix, Index + 1);
    return std::nullopt;
  }

  if (isSubmoduleLike(Type)) {
    if (std::optional<OptionType> Known = nestedType(Type, Suffix[Index]))
      return deriveTypeForSuffix(*Known, Suffix, Index + 1);
    if (std::optional<OptionType> Freeform = nestedType(Type, "freeformType"))
      return deriveTypeForSuffix(*Freeform, Suffix, Index + 1);
  }

  return std::nullopt;
}

std::optional<OptionType>
nixd::option_navigation::deriveTypeFromResolvedInfo(
    const ResolvedOptionInfo &Info, const std::vector<std::string> &Suffix) {
  if (!Info.Description.Type)
    return std::nullopt;
  return deriveTypeForSuffix(*Info.Description.Type, Suffix);
}

std::vector<OptionType>
nixd::option_navigation::childTypesForStep(const OptionType &Type,
                                           const ChildStep &Step) {
  const std::string LowerName = lowerTypeName(Type);

  if (LowerName == "nullor") {
    if (std::optional<OptionType> Elem = nullOrTypeFor(Type))
      return childTypesForStep(*Elem, Step);
  }
  if (LowerName == "unique") {
    if (std::optional<OptionType> Elem = elemTypeFor(Type, LowerName))
      return childTypesForStep(*Elem, Step);
  }

  std::vector<OptionType> Out;
  for (const OptionType &Alternative : alternativeTypesFor(Type, LowerName)) {
    std::vector<OptionType> Nested = childTypesForStep(Alternative, Step);
    Out.insert(Out.end(), std::make_move_iterator(Nested.begin()),
               std::make_move_iterator(Nested.end()));
  }
  if (!Out.empty())
    return Out;

  switch (Step.Kind) {
  case ChildKind::ListElement:
    if (LowerName == "listof" || LowerName == "loaof" ||
        isNonEmptyListType(Type)) {
      if (std::optional<OptionType> Elem = elemTypeFor(Type, LowerName))
        Out.emplace_back(std::move(*Elem));
    }
    break;
  case ChildKind::AttrValue:
    if (LowerName == "attrsof" || LowerName == "lazyattrsof" ||
        LowerName == "attrswith" || LowerName == "loaof") {
      if (std::optional<OptionType> Elem = elemTypeFor(Type, LowerName))
        Out.emplace_back(std::move(*Elem));
    } else if (isSubmoduleLike(Type)) {
      if (std::optional<OptionType> Known = nestedType(Type, Step.Name))
        Out.emplace_back(std::move(*Known));
      else if (std::optional<OptionType> Freeform =
                   nestedType(Type, "freeformType"))
        Out.emplace_back(std::move(*Freeform));
    }
    break;
  case ChildKind::FunctionBody:
    if (LowerName == "functionto") {
      if (std::optional<OptionType> Result = functionResultTypeFor(Type))
        Out.emplace_back(std::move(*Result));
    }
    break;
  }

  return Out;
}

std::vector<OptionType>
nixd::option_navigation::descendValuePath(
    const OptionType &Type, const std::vector<ChildStep> &Steps) {
  std::vector<OptionType> Current{Type};
  for (const ChildStep &Step : Steps) {
    std::vector<OptionType> Next;
    for (const OptionType &Candidate : Current) {
      std::vector<OptionType> Nested = childTypesForStep(Candidate, Step);
      Next.insert(Next.end(), std::make_move_iterator(Nested.begin()),
                  std::make_move_iterator(Nested.end()));
    }
    if (Next.empty())
      return {};
    Current = std::move(Next);
  }
  return Current;
}

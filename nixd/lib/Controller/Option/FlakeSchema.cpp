#include "FlakeSchema.h"

#include "Navigation.h"

#include <algorithm>
#include <iterator>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

#include <nixf/Basic/Nodes/Attrs.h>
#include <nixf/Sema/ParentMap.h>

using namespace nixd;
using namespace nixf;

namespace {

constexpr std::string_view ProviderName = "flake";

OptionType scalar(std::string Name, std::string Description) {
  OptionType Type;
  Type.Name = std::move(Name);
  Type.Description = std::move(Description);
  return Type;
}

OptionType stringType() { return scalar("str", "string"); }

OptionType boolType() { return scalar("bool", "boolean"); }

OptionType pathType() { return scalar("path", "path"); }

OptionType anythingType() {
  return scalar("anything", "any Nix value");
}

OptionType eitherType(OptionType Left, OptionType Right,
                      std::string Description) {
  OptionType Type;
  Type.Name = "either";
  Type.Description = std::move(Description);
  Type.NestedTypes.emplace("left", std::move(Left));
  Type.NestedTypes.emplace("right", std::move(Right));
  return Type;
}

OptionType attrsOfType(OptionType Elem, std::string Description) {
  OptionType Type;
  Type.Name = "attrsOf";
  Type.Description = std::move(Description);
  Type.NestedTypes.emplace("elemType", std::move(Elem));
  return Type;
}

OptionType functionToType(OptionType Result, std::string Description) {
  OptionType Type;
  Type.Name = "functionTo";
  Type.Description = std::move(Description);
  Type.NestedTypes.emplace("resultType", std::move(Result));
  return Type;
}

void addKnownSubOption(OptionType &Type, std::string Name, OptionType Child,
                       bool Required = false) {
  OptionType::KnownSubOption Summary;
  Summary.Required = Required;
  Type.KnownSubOptions.emplace(Name, Summary);
  Type.NestedTypes.emplace(std::move(Name), std::move(Child));
}

OptionType submoduleType(std::string Description,
                         bool KnownSubOptionsComplete = true) {
  OptionType Type;
  Type.Name = "submodule";
  Type.Description = std::move(Description);
  Type.KnownSubOptionsComplete = KnownSubOptionsComplete;
  return Type;
}

OptionType inputOverrideType();

OptionType inputSpecType() {
  OptionType Type = submoduleType("flake input attribute set", false);

  addKnownSubOption(Type, "url", stringType());
  addKnownSubOption(Type, "flake", boolType());
  addKnownSubOption(Type, "inputs",
                    attrsOfType(inputOverrideType(),
                                "attribute set of input overrides"));
  addKnownSubOption(Type, "follows", stringType());

  addKnownSubOption(Type, "type", stringType());
  addKnownSubOption(Type, "owner", stringType());
  addKnownSubOption(Type, "repo", stringType());
  addKnownSubOption(Type, "ref", stringType());
  addKnownSubOption(Type, "rev", stringType());
  addKnownSubOption(Type, "narHash", stringType());
  addKnownSubOption(Type, "dir", stringType());
  addKnownSubOption(Type, "host", stringType());
  addKnownSubOption(Type, "id", stringType());
  addKnownSubOption(Type, "path", eitherType(pathType(), stringType(),
                                             "path or string"));

  addKnownSubOption(Type, "submodules", boolType());
  addKnownSubOption(Type, "shallow", boolType());
  addKnownSubOption(Type, "allRefs", boolType());
  addKnownSubOption(Type, "lfs", boolType());

  return Type;
}

OptionType inputOverrideType() {
  OptionType Type = submoduleType("flake input override", false);
  addKnownSubOption(Type, "follows", stringType());
  addKnownSubOption(Type, "inputs",
                    attrsOfType(submoduleType("flake input override", false),
                                "attribute set of input overrides"));
  return Type;
}

OptionType outputAppType() {
  OptionType Type = submoduleType("flake app", true);
  addKnownSubOption(Type, "type", stringType(), true);
  addKnownSubOption(Type, "program", stringType(), true);
  return Type;
}

OptionType templateType() {
  OptionType Type = submoduleType("flake template", true);
  addKnownSubOption(Type, "path", pathType(), true);
  addKnownSubOption(Type, "description", stringType());
  addKnownSubOption(Type, "welcomeText", stringType());
  return Type;
}

OptionType outputsResultType() {
  OptionType Type = submoduleType("flake outputs", false);
  OptionType Any = anythingType();

  addKnownSubOption(Type, "checks",
                    attrsOfType(attrsOfType(Any, "attribute set of checks"),
                                "attribute set of systems"));
  addKnownSubOption(Type, "packages",
                    attrsOfType(attrsOfType(Any, "attribute set of packages"),
                                "attribute set of systems"));
  addKnownSubOption(Type, "legacyPackages",
                    attrsOfType(attrsOfType(Any, "attribute set of packages"),
                                "attribute set of systems"));
  addKnownSubOption(Type, "apps",
                    attrsOfType(attrsOfType(outputAppType(),
                                            "attribute set of apps"),
                                "attribute set of systems"));
  addKnownSubOption(Type, "devShells",
                    attrsOfType(attrsOfType(Any, "attribute set of dev shells"),
                                "attribute set of systems"));
  addKnownSubOption(Type, "formatter",
                    attrsOfType(Any, "attribute set of systems"));

  addKnownSubOption(Type, "nixosConfigurations",
                    attrsOfType(Any, "attribute set of NixOS configurations"));
  addKnownSubOption(Type, "nixosModules",
                    attrsOfType(Any, "attribute set of NixOS modules"));
  addKnownSubOption(Type, "overlays",
                    attrsOfType(Any, "attribute set of overlays"));
  addKnownSubOption(Type, "templates",
                    attrsOfType(templateType(), "attribute set of templates"));
  addKnownSubOption(Type, "hydraJobs",
                    attrsOfType(Any, "attribute set of Hydra jobs"));

  return Type;
}

OptionDescription makeDescription(OptionType Type, std::string Description) {
  OptionDescription Desc;
  Desc.Type = std::move(Type);
  Desc.Description = std::move(Description);
  return Desc;
}

OptionDescription rootDescription() {
  OptionType Type = submoduleType("flake attribute set", true);
  addKnownSubOption(Type, "description", stringType());
  addKnownSubOption(Type, "inputs",
                    attrsOfType(inputSpecType(),
                                "attribute set of flake inputs"));
  addKnownSubOption(Type, "outputs",
                    functionToType(outputsResultType(),
                                   "function that evaluates to flake outputs"),
                    true);
  addKnownSubOption(Type, "nixConfig",
                    attrsOfType(anythingType(),
                                "attribute set of Nix configuration values"));

  return makeDescription(std::move(Type),
                         "The top-level attribute set of a flake.nix file.");
}

std::optional<OptionDescription>
exactDescription(const std::vector<std::string> &Scope) {
  if (Scope.empty())
    return rootDescription();

  if (Scope.size() == 1) {
    if (Scope[0] == "description")
      return makeDescription(
          stringType(), "A short human-readable description of the flake.");
    if (Scope[0] == "inputs")
      return makeDescription(
          attrsOfType(inputSpecType(), "attribute set of flake inputs"),
          "Inputs consumed by this flake.");
    if (Scope[0] == "outputs")
      return makeDescription(
          functionToType(outputsResultType(),
                         "function that evaluates to flake outputs"),
          "A function from resolved inputs to this flake's outputs.");
    if (Scope[0] == "nixConfig")
      return makeDescription(
          attrsOfType(anythingType(),
                      "attribute set of Nix configuration values"),
          "Nix configuration values to apply when evaluating this flake.");
  }

  return std::nullopt;
}

std::vector<ResolvedOptionInfo> makeInfo(OptionDescription Desc) {
  std::vector<ResolvedOptionInfo> Infos;
  Infos.push_back(ResolvedOptionInfo{.ProviderName = std::string(ProviderName),
                                     .Description = std::move(Desc)});
  return Infos;
}

std::optional<OptionDescription>
deriveDescription(const std::vector<std::string> &Scope) {
  if (std::optional<OptionDescription> Exact = exactDescription(Scope))
    return Exact;

  if (Scope.size() > 1 && Scope.front() == "outputs") {
    OptionType Outputs = outputsResultType();
    std::vector<std::string> Suffix(Scope.begin() + 1, Scope.end());
    if (std::optional<OptionType> Derived =
            option_navigation::deriveTypeForSuffix(Outputs, Suffix)) {
      OptionDescription Desc;
      Desc.Type = std::move(*Derived);
      return Desc;
    }
  }

  OptionDescription Root = rootDescription();
  if (!Root.Type)
    return std::nullopt;
  if (std::optional<OptionType> Derived =
          option_navigation::deriveTypeForSuffix(*Root.Type, Scope)) {
    OptionDescription Desc;
    Desc.Type = std::move(*Derived);
    return Desc;
  }
  return std::nullopt;
}

void appendFieldsFromType(std::vector<ResolvedOptionField> &Fields,
                          const OptionType &Type, const std::string &Prefix);

void appendField(std::vector<ResolvedOptionField> &Fields, std::string Name,
                 std::optional<OptionType> Type) {
  OptionField Field;
  Field.Name = std::move(Name);
  if (Type) {
    OptionDescription Desc;
    Desc.Type = std::move(*Type);
    Field.Description = std::move(Desc);
  }
  Fields.push_back(ResolvedOptionField{.ProviderName = std::string(ProviderName),
                                       .Field = std::move(Field)});
}

void appendFieldsFromType(std::vector<ResolvedOptionField> &Fields,
                          const OptionType &Type, const std::string &Prefix) {
  const std::string LowerName = option_navigation::lowerTypeName(Type);

  if (LowerName == "nullor" || LowerName == "unique") {
    if (std::optional<OptionType> Elem =
            LowerName == "nullor"
                ? option_navigation::nullOrTypeFor(Type)
                : option_navigation::elemTypeFor(Type, LowerName))
      appendFieldsFromType(Fields, *Elem, Prefix);
    return;
  }

  if (LowerName == "functionto") {
    if (std::optional<OptionType> Result =
            option_navigation::functionResultTypeFor(Type))
      appendFieldsFromType(Fields, *Result, Prefix);
    return;
  }

  for (const OptionType &Alternative :
       option_navigation::alternativeTypesFor(Type, LowerName))
    appendFieldsFromType(Fields, Alternative, Prefix);

  if (!option_navigation::isSubmoduleLike(Type))
    return;

  std::vector<std::string> Added;
  for (const auto &[Name, Child] : Type.NestedTypes) {
    if (Name == "freeformType" || !Name.starts_with(Prefix))
      continue;
    appendField(Fields, Name, Child);
    Added.push_back(Name);
  }

  for (const auto &[Name, Summary] : Type.KnownSubOptions) {
    (void)Summary;
    if (!Name.starts_with(Prefix))
      continue;
    if (std::find(Added.begin(), Added.end(), Name) != Added.end())
      continue;
    appendField(Fields, Name, std::nullopt);
  }
}

} // namespace

bool nixd::flake_schema::isFlakeFile(std::string_view File) {
  return File == "flake.nix" || File.ends_with("/flake.nix");
}

bool nixd::flake_schema::isInsideOutputsBody(const Node &Desc,
                                             const ParentMapAnalysis &PM) {
  const Node *Current = &Desc;
  std::unordered_set<const Node *> Seen;
  while (Current && Seen.insert(Current).second) {
    const Node *Parent = PM.query(*Current);
    if (!Parent)
      return false;

    if (Parent->kind() == Node::NK_Binding) {
      const auto &Bind = static_cast<const Binding &>(*Parent);
      const auto &Names = Bind.path().names();
      if (!Names.empty() && Names.front() && Names.front()->isStatic() &&
          Names.front()->staticName() == "outputs")
        return Bind.value().get() == Current;
    }

    Current = Parent;
  }
  return false;
}

std::vector<std::string> nixd::flake_schema::outputsBodyScope(
    const std::vector<std::string> &Scope) {
  std::vector<std::string> Out;
  Out.reserve(Scope.size() + 1);
  Out.emplace_back("outputs");
  Out.insert(Out.end(), Scope.begin(), Scope.end());
  return Out;
}

std::vector<ResolvedOptionInfo>
nixd::flake_schema::resolve(const std::vector<std::string> &Scope) {
  if (std::optional<OptionDescription> Desc = exactDescription(Scope))
    return makeInfo(std::move(*Desc));
  return {};
}

std::vector<ResolvedOptionInfo>
nixd::flake_schema::resolveDerived(const std::vector<std::string> &Scope) {
  if (std::optional<OptionDescription> Desc = deriveDescription(Scope))
    return makeInfo(std::move(*Desc));
  return {};
}

std::vector<ResolvedOptionField>
nixd::flake_schema::complete(const std::vector<std::string> &Scope,
                             const std::string &Prefix) {
  std::vector<ResolvedOptionField> Fields;
  if (std::optional<OptionDescription> Desc = exactDescription(Scope)) {
    if (Desc->Type)
      appendFieldsFromType(Fields, *Desc->Type, Prefix);
  }
  return Fields;
}

std::vector<ResolvedOptionField>
nixd::flake_schema::completeDerived(const std::vector<std::string> &Scope,
                                    const std::string &Prefix) {
  std::vector<ResolvedOptionField> Fields = complete(Scope, Prefix);
  if (!Fields.empty())
    return Fields;

  if (std::optional<OptionDescription> Desc = deriveDescription(Scope)) {
    if (Desc->Type)
      appendFieldsFromType(Fields, *Desc->Type, Prefix);
  }
  return Fields;
}

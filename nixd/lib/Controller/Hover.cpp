/// \file
/// \brief Implementation of [Hover Request].
/// [Hover Request]:
/// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_hover

#include "AST.h"
#include "CheckReturn.h"
#include "Convert.h"

#include "Option/FlakeSchema.h"
#include "Option/Navigation.h"
#include "nixd/Controller/Controller.h"
#include "nixd/Controller/FlakeInputInspect.h"
#include "nixd/Controller/Option.h"
#include "nixd/Protocol/AttrSet.h"

#include <nixf/Basic/Nodes/Lambda.h>
#include <nixf/Basic/Nodes/Simple.h>

#include <boost/asio/post.hpp>

#include <llvm/Support/Error.h>

#include <algorithm>
#include <functional>
#include <optional>
#include <semaphore>
#include <set>
#include <sstream>
#include <vector>

using namespace nixd;
using namespace llvm::json;
using namespace nixf;
using namespace lspserver;

namespace {

class OptionsHoverProvider {
  AttrSetClient &Client;

public:
  OptionsHoverProvider(AttrSetClient &Client) : Client(Client) {}
  std::optional<OptionDescription>
  resolveHover(const std::vector<std::string> &Scope) {
    std::binary_semaphore Ready(0);
    std::optional<OptionDescription> Desc;
    auto OnReply = [&Ready, &Desc](llvm::Expected<OptionInfoResponse> Resp) {
      if (Resp)
        Desc = *Resp;
      else
        elog("options hover: {0}", Resp.takeError());
      Ready.release();
    };

    Client.optionInfo(Scope, std::move(OnReply));
    Ready.acquire();

    return Desc;
  }
};

std::string renderOptionTypeInline(const OptionType &Type) {
  std::ostringstream OS;
  if (Type.Name)
    OS << "`" << *Type.Name << "`";
  if (Type.Description) {
    if (OS.tellp() > 0)
      OS << " - ";
    OS << *Type.Description;
  }
  return OS.str();
}

std::string mkOptionMarkdown(const OptionDescription &Desc);

std::optional<std::vector<std::string>>
staticAttrPathPrefix(const Node &N, const ParentMapAnalysis &PM) {
  const Node *NameNode = PM.upTo(N, Node::NK_AttrName);
  if (!NameNode)
    return std::nullopt;

  const Node *PathNode = PM.query(*NameNode);
  if (!PathNode || PathNode->kind() != Node::NK_AttrPath)
    return std::nullopt;

  const auto &Path = static_cast<const AttrPath &>(*PathNode);
  std::vector<std::string> Prefix;
  for (const auto &Name : Path.names()) {
    if (!Name || !Name->isStatic())
      return std::nullopt;
    Prefix.emplace_back(Name->staticName());
    if (Name.get() == NameNode)
      return Prefix;
  }

  return std::nullopt;
}

std::optional<std::vector<std::string>>
staticBindingPath(const Binding &Binding) {
  std::vector<std::string> Path;
  for (const auto &Name : Binding.path().names()) {
    if (!Name || !Name->isStatic())
      return std::nullopt;
    Path.emplace_back(Name->staticName());
  }
  if (Path.empty())
    return std::nullopt;
  return Path;
}

bool isListElementChild(const ExprList &List, const Node &Child) {
  for (const auto &Element : List.elements())
    if (Element.get() == &Child)
      return true;
  return false;
}

const Node *enclosingBindingNode(const Binding &Binding,
                                 const ParentMapAnalysis &PM) {
  const Node *Current = PM.query(Binding);
  std::set<const Node *> Seen{&Binding};
  while (Current && Seen.insert(Current).second) {
    if (Current->kind() == Node::NK_Binding)
      return Current;
    Current = PM.query(*Current);
  }
  return nullptr;
}

bool isPrefixOrEqual(const std::vector<std::string> &Prefix,
                     const std::vector<std::string> &Scope) {
  return Prefix.size() <= Scope.size() &&
         std::equal(Prefix.begin(), Prefix.end(), Scope.begin());
}

struct OptionFieldHoverContext {
  std::vector<std::string> Scope;
  std::vector<option_navigation::ChildStep> ValuePath;
};

std::optional<OptionFieldHoverContext>
findOptionFieldHoverContext(const Node &N, const ParentMapAnalysis &PM) {
  std::optional<std::vector<std::string>> AttrPrefix =
      staticAttrPathPrefix(N, PM);
  if (!AttrPrefix || AttrPrefix->empty())
    return std::nullopt;

  const Node *BindingNode = PM.upTo(N, Node::NK_Binding);
  if (!BindingNode)
    return std::nullopt;

  const Node *Current = PM.upExpr(N);
  if (!Current)
    return std::nullopt;

  const Binding *SelectedBinding = nullptr;
  std::vector<std::string> SelectedScope;
  std::set<const Node *> SeenBindings;
  while (BindingNode && SeenBindings.insert(BindingNode).second) {
    const auto &Binding = static_cast<const nixf::Binding &>(*BindingNode);
    if (std::optional<std::vector<std::string>> Scope =
            findOptionBindingScope(Binding, PM)) {
      if (!SelectedBinding) {
        SelectedBinding = &Binding;
        SelectedScope = std::move(*Scope);
      } else if (!isPrefixOrEqual(*Scope, SelectedScope)) {
        SelectedBinding = &Binding;
        SelectedScope = std::move(*Scope);
      } else {
        break;
      }
    }

    BindingNode = enclosingBindingNode(Binding, PM);
  }

  if (!SelectedBinding || !SelectedBinding->value())
    return std::nullopt;

  const Expr *OuterValue = SelectedBinding->value().get();
  std::vector<option_navigation::ChildStep> ReversedPath;
  while (Current && Current != OuterValue) {
    if (PM.isRoot(*Current))
      break;
    const Node *Parent = PM.query(*Current);
    if (!Parent)
      break;

    if (Parent->kind() == Node::NK_ExprList &&
        isListElementChild(static_cast<const ExprList &>(*Parent), *Current)) {
      ReversedPath.push_back(option_navigation::ChildStep{
          .Kind = option_navigation::ChildKind::ListElement});
    } else if (Parent->kind() == Node::NK_Binding &&
               Parent != SelectedBinding &&
               static_cast<const Binding *>(Parent)->value().get() == Current) {
      if (std::optional<std::vector<std::string>> Path =
              staticBindingPath(static_cast<const Binding &>(*Parent))) {
        for (auto It = Path->rbegin(); It != Path->rend(); ++It)
          ReversedPath.push_back(option_navigation::ChildStep{
              .Kind = option_navigation::ChildKind::AttrValue, .Name = *It});
      }
    } else if (Parent->kind() == Node::NK_ExprLambda &&
               static_cast<const ExprLambda *>(Parent)->body() == Current) {
      ReversedPath.push_back(option_navigation::ChildStep{
          .Kind = option_navigation::ChildKind::FunctionBody});
    }

    Current = Parent;
  }

  std::reverse(ReversedPath.begin(), ReversedPath.end());
  for (const std::string &Name : *AttrPrefix)
    ReversedPath.push_back(option_navigation::ChildStep{
        .Kind = option_navigation::ChildKind::AttrValue, .Name = Name});

  return OptionFieldHoverContext{.Scope = std::move(SelectedScope),
                                 .ValuePath = std::move(ReversedPath)};
}

std::optional<Hover> hoverOptionValueField(
    const Node &N, llvm::StringRef Src,
    const std::vector<option_navigation::ChildStep> &ValuePath,
    const std::vector<ResolvedOptionInfo> &BaseOptionInfos) {
  for (const ResolvedOptionInfo &Info : BaseOptionInfos) {
    if (!Info.Description.Type)
      continue;

    std::vector<OptionType> Expected =
        option_navigation::descendValuePath(*Info.Description.Type, ValuePath);
    for (OptionType &Type : Expected) {
      OptionDescription Desc;
      Desc.Type = std::move(Type);
      std::string Docs = mkOptionMarkdown(Desc);
      return Hover{
          .contents =
              MarkupContent{
                  .kind = MarkupKind::Markdown,
                  .value = std::move(Docs),
              },
          .range = toLSPRange(Src, N.range()),
      };
    }
  }

  return std::nullopt;
}

std::string mkOptionMarkdown(const OptionDescription &Desc) {
  std::ostringstream OS;

  OS << "\"type\": ";
  if (Desc.Type) {
    const std::string Rendered = renderOptionTypeInline(*Desc.Type);
    OS << (Rendered.empty() ? "? (missing type)" : Rendered);
  } else {
    OS << "? (missing type)";
  }

  if (Desc.Description)
    OS << "  \n\"description\": " << *Desc.Description;

  return OS.str();
}

bool hasHoverOptionTypeMetadata(const OptionType &Type) {
  return Type.Name || Type.Description || !Type.NestedTypes.empty() ||
         !Type.EnumValues.empty() || Type.String || Type.Path ||
         !Type.KnownSubOptions.empty() || !Type.KnownSubOptionsComplete;
}

void normalizeNamespaceTypeLabels(OptionType &Type) {
  for (auto &[Name, Child] : Type.NestedTypes) {
    (void)Name;
    normalizeNamespaceTypeLabels(Child);
  }

  if (!Type.Name && !Type.Description &&
      (option_navigation::hasSubOptionMetadata(Type) ||
       !Type.NestedTypes.empty()))
    Type.Name = "namespace";
}

OptionDescription normalizeHoverOptionDescription(OptionDescription Desc) {
  if (Desc.Type)
    normalizeNamespaceTypeLabels(*Desc.Type);
  return Desc;
}

bool hasHoverOptionDescription(const OptionDescription &Desc) {
  return Desc.Description || Desc.Example || !Desc.Declarations.empty() ||
         !Desc.Definitions.empty() ||
         (Desc.Type && hasHoverOptionTypeMetadata(*Desc.Type));
}

std::optional<OptionDescription>
synthesizeNamespaceDescription(const std::vector<ResolvedOptionField> &Fields) {
  if (Fields.empty())
    return std::nullopt;

  OptionType Type;
  Type.Name = "namespace";
  for (const ResolvedOptionField &Entry : Fields) {
    Type.KnownSubOptions.try_emplace(Entry.Field.Name);
    if (!Entry.Field.Description)
      continue;

    OptionType Child;
    bool HasChild = false;
    if (Entry.Field.Description->Type) {
      Child = *Entry.Field.Description->Type;
      HasChild = true;
    }
    if (!Child.Description && Entry.Field.Description->Description) {
      Child.Description = *Entry.Field.Description->Description;
      HasChild = true;
    }
    if (!HasChild)
      continue;

    normalizeNamespaceTypeLabels(Child);
    if (hasHoverOptionTypeMetadata(Child))
      Type.NestedTypes[Entry.Field.Name] = std::move(Child);
  }

  OptionDescription Desc;
  Desc.Type = std::move(Type);
  return Desc;
}

std::optional<OptionDescription> resolveHoverOptionDescription(
    const std::vector<std::string> &Scope,
    const std::function<std::vector<ResolvedOptionInfo>(
        const std::vector<std::string> &)> &Resolve,
    const std::function<std::vector<ResolvedOptionInfo>(
        const std::vector<std::string> &)> &ResolveDerived,
    const std::function<std::vector<ResolvedOptionField>(
        const std::vector<std::string> &, const std::string &)>
        &CompleteDerived) {
  std::optional<OptionDescription> Fallback;
  for (const ResolvedOptionInfo &Info : ResolveDerived(Scope)) {
    OptionDescription Desc = normalizeHoverOptionDescription(Info.Description);
    if (hasHoverOptionDescription(Desc))
      return Desc;
    if (!Fallback)
      Fallback = std::move(Desc);
  }

  for (size_t PrefixLen = Scope.size(); PrefixLen > 0; --PrefixLen) {
    std::vector<std::string> ParentScope(Scope.begin(),
                                         Scope.begin() + PrefixLen);
    std::vector<std::string> Suffix(Scope.begin() + PrefixLen, Scope.end());
    if (Suffix.empty())
      continue;

    for (const ResolvedOptionInfo &Info : Resolve(ParentScope)) {
      std::optional<OptionType> Derived =
          option_navigation::deriveTypeFromResolvedInfo(Info, Suffix);
      if (!Derived)
        continue;

      OptionDescription Desc;
      Desc.Type = std::move(*Derived);
      Desc = normalizeHoverOptionDescription(std::move(Desc));
      if (hasHoverOptionDescription(Desc))
        return Desc;
      if (!Fallback)
        Fallback = std::move(Desc);
    }
  }

  if (std::optional<OptionDescription> Namespace =
          synthesizeNamespaceDescription(CompleteDerived(Scope, ""))) {
    *Namespace = normalizeHoverOptionDescription(std::move(*Namespace));
    if (hasHoverOptionDescription(*Namespace))
      return Namespace;
    if (!Fallback)
      Fallback = std::move(*Namespace);
  }

  return Fallback;
}

struct ModuleInputContext {
  enum class Kind {
    Named,
    Ellipsis,
  };

  Kind InputKind = Kind::Named;
  std::string Name;
  const ExprLambda *Lambda = nullptr;
  const Node *RangeNode = nullptr;
};

std::optional<ModuleInputContext>
moduleInputFromDefinitionSyntax(const Node &Syntax,
                                const ParentMapAnalysis &PM) {
  if (const Node *FormalNode = PM.upTo(Syntax, Node::NK_Formal)) {
    const auto &Formal = static_cast<const nixf::Formal &>(*FormalNode);
    const Node *LambdaNode = PM.upTo(*FormalNode, Node::NK_ExprLambda);
    if (!LambdaNode)
      return std::nullopt;

    const auto &Lambda = static_cast<const ExprLambda &>(*LambdaNode);
    if (Formal.isEllipsis()) {
      if (&Formal.ellipsis() != &Syntax && FormalNode != &Syntax)
        return std::nullopt;
      return ModuleInputContext{
          .InputKind = ModuleInputContext::Kind::Ellipsis,
          .Lambda = &Lambda,
          .RangeNode = &Syntax,
      };
    }

    const auto *ID = Formal.id();
    if (!ID || ID != &Syntax)
      return std::nullopt;
    return ModuleInputContext{
        .InputKind = ModuleInputContext::Kind::Named,
        .Name = ID->name(),
        .Lambda = &Lambda,
        .RangeNode = &Syntax,
    };
  }

  if (Syntax.kind() != Node::NK_Identifier)
    return std::nullopt;

  const Node *ArgNode = PM.upTo(Syntax, Node::NK_LambdaArg);
  if (!ArgNode)
    return std::nullopt;

  const auto &Arg = static_cast<const LambdaArg &>(*ArgNode);
  const auto *ID = Arg.id();
  if (!ID || ID != &Syntax)
    return std::nullopt;

  const Node *LambdaNode = PM.upTo(*ArgNode, Node::NK_ExprLambda);
  if (!LambdaNode)
    return std::nullopt;

  return ModuleInputContext{
      .InputKind = ModuleInputContext::Kind::Named,
      .Name = ID->name(),
      .Lambda = static_cast<const ExprLambda *>(LambdaNode),
      .RangeNode = &Syntax,
  };
}

std::optional<ModuleInputContext>
findModuleInputContext(const Node &N, const VariableLookupAnalysis &VLA,
                       const ParentMapAnalysis &PM) {
  if (std::optional<ModuleInputContext> Context =
          moduleInputFromDefinitionSyntax(N, PM))
    return Context;

  const Node *ExprNode = PM.upExpr(N);
  if (!ExprNode || ExprNode->kind() != Node::NK_ExprVar)
    return std::nullopt;

  const auto &Var = static_cast<const ExprVar &>(*ExprNode);
  const auto Lookup = VLA.query(Var);
  if (!Lookup.Def || !Lookup.Def->syntax())
    return std::nullopt;

  std::optional<ModuleInputContext> Context =
      moduleInputFromDefinitionSyntax(*Lookup.Def->syntax(), PM);
  if (!Context)
    return std::nullopt;

  Context->RangeNode = ExprNode;
  return Context;
}

bool isTopLevelLambda(const ExprLambda &Lambda, const ParentMapAnalysis &PM) {
  const Node *Current = &Lambda;
  while (Current) {
    if (PM.isRoot(*Current))
      return true;

    const Node *Parent = PM.query(*Current);
    if (!Parent)
      return true;

    if (Parent->kind() != Node::NK_ExprParen)
      return false;
    Current = Parent;
  }
  return false;
}

std::optional<OptionType> findSubmoduleType(const OptionType &Type,
                                            unsigned Depth = 0) {
  if (Depth > 8)
    return std::nullopt;
  if (option_navigation::isSubmoduleLike(Type))
    return Type;

  const std::string LowerName = option_navigation::lowerTypeName(Type);
  if (LowerName == "nullor") {
    if (std::optional<OptionType> Elem =
            option_navigation::nullOrTypeFor(Type))
      return findSubmoduleType(*Elem, Depth + 1);
  }
  if (LowerName == "unique") {
    if (std::optional<OptionType> Elem =
            option_navigation::elemTypeFor(Type, LowerName))
      return findSubmoduleType(*Elem, Depth + 1);
  }

  for (const OptionType &Alternative :
       option_navigation::alternativeTypesFor(Type, LowerName)) {
    if (std::optional<OptionType> Found =
            findSubmoduleType(Alternative, Depth + 1))
      return Found;
  }

  return std::nullopt;
}

option_navigation::ChildStep
toNavigationStep(const OptionValueChildStep &Step) {
  switch (Step.Kind) {
  case OptionValueChildKind::ListElement:
    return option_navigation::ChildStep{
        .Kind = option_navigation::ChildKind::ListElement};
  case OptionValueChildKind::AttrValue:
    return option_navigation::ChildStep{
        .Kind = option_navigation::ChildKind::AttrValue, .Name = Step.Name};
  case OptionValueChildKind::FunctionBody:
    return option_navigation::ChildStep{
        .Kind = option_navigation::ChildKind::FunctionBody};
  }
  __builtin_unreachable();
}

std::vector<option_navigation::ChildStep>
toNavigationPath(const std::vector<OptionValueChildStep> &Steps) {
  std::vector<option_navigation::ChildStep> Result;
  Result.reserve(Steps.size());
  for (const OptionValueChildStep &Step : Steps)
    Result.emplace_back(toNavigationStep(Step));
  return Result;
}

std::optional<OptionType> resolveSubmoduleTypeForLambda(
    const ExprLambda &Lambda, const ParentMapAnalysis &PM,
    const std::function<std::vector<ResolvedOptionInfo>(
        const std::vector<std::string> &)> &Resolve) {
  std::optional<OptionValueContext> Context =
      findOptionValueContext(Lambda, PM, Lambda.lCur().position());
  if (!Context)
    return std::nullopt;

  std::vector<option_navigation::ChildStep> ValuePath =
      toNavigationPath(Context->ValuePath);
  for (const ResolvedOptionInfo &Info : Resolve(Context->Scope)) {
    if (!Info.Description.Type)
      continue;

    std::vector<OptionType> Types =
        option_navigation::descendValuePath(*Info.Description.Type, ValuePath);
    for (const OptionType &Type : Types) {
      if (std::optional<OptionType> Submodule = findSubmoduleType(Type))
        return Submodule;
    }
  }

  return std::nullopt;
}

struct ModuleInputs {
  std::set<std::string> Core;
  std::set<std::string> ModuleArgs;
  std::set<std::string> SpecialArgs;
};

std::vector<std::string> appendScope(std::vector<std::string> Scope,
                                     std::initializer_list<std::string> Suffix) {
  Scope.insert(Scope.end(), Suffix.begin(), Suffix.end());
  return Scope;
}

std::set<std::string> valueAttrNameSet(
    const std::vector<ResolvedOptionInfo> &Infos) {
  std::set<std::string> Names;
  for (const ResolvedOptionInfo &Info : Infos)
    Names.insert(Info.Description.ValueAttrNames.begin(),
                 Info.Description.ValueAttrNames.end());
  return Names;
}

std::optional<std::vector<std::string>>
moduleInputScopeForLambda(const ExprLambda &Lambda,
                          const ParentMapAnalysis &PM) {
  if (std::optional<OptionValueContext> Context =
          findOptionValueContext(Lambda, PM, Lambda.lCur().position()))
    return std::move(Context->Scope);
  if (isTopLevelLambda(Lambda, PM))
    return std::vector<std::string>{};
  return std::nullopt;
}

ModuleInputs resolveModuleInputs(
    const std::vector<std::string> &Scope,
    const std::function<std::vector<ResolvedOptionInfo>(
        const std::vector<std::string> &)> &Resolve) {
  ModuleInputs Inputs;
  Inputs.Core = {"config", "lib", "options", "specialArgs"};
  Inputs.ModuleArgs =
      valueAttrNameSet(Resolve(appendScope(Scope, {"_module", "args"})));
  Inputs.SpecialArgs =
      valueAttrNameSet(Resolve(appendScope(Scope, {"_module", "specialArgs"})));
  return Inputs;
}

std::set<std::string> allModuleInputNames(const ModuleInputs &Inputs) {
  std::set<std::string> Names = Inputs.Core;
  Names.insert(Inputs.ModuleArgs.begin(), Inputs.ModuleArgs.end());
  Names.insert(Inputs.SpecialArgs.begin(), Inputs.SpecialArgs.end());
  return Names;
}

std::vector<std::string> moduleInputSources(std::string_view Name,
                                            const ModuleInputs &Inputs) {
  std::vector<std::string> Sources;
  if (Inputs.Core.contains(std::string(Name)))
    Sources.emplace_back("module system");
  if (Inputs.ModuleArgs.contains(std::string(Name)))
    Sources.emplace_back("_module.args");
  if (Inputs.SpecialArgs.contains(std::string(Name)))
    Sources.emplace_back("specialArgs");
  return Sources;
}

std::string joinSources(const std::vector<std::string> &Sources) {
  std::ostringstream OS;
  for (size_t I = 0; I < Sources.size(); ++I) {
    if (I)
      OS << ", ";
    OS << "`" << Sources[I] << "`";
  }
  return OS.str();
}

void appendModuleInputList(std::ostringstream &OS, const ModuleInputs &Inputs) {
  for (const std::string &Name : allModuleInputNames(Inputs)) {
    std::vector<std::string> Sources = moduleInputSources(Name, Inputs);
    OS << "- `" << Name << "`";
    if (!Sources.empty())
      OS << " (" << joinSources(Sources) << ")";
    OS << "\n";
  }
}

std::string mkModuleInputMarkdown(std::string_view Name,
                                  const ModuleInputs &Inputs,
                                  const OptionType *SubmoduleType) {
  std::ostringstream OS;
  OS << "## Module Input\n\n";
  OS << "`" << Name << "`\n\n";
  std::vector<std::string> Sources = moduleInputSources(Name, Inputs);
  OS << "Provided by: "
     << (Sources.empty() ? "unknown module argument source"
                         : joinSources(Sources))
     << ".";

  if (SubmoduleType && (Name == "config" || Name == "options")) {
    OptionDescription Desc;
    Desc.Type = *SubmoduleType;
    OS << "\n\n" << mkOptionMarkdown(Desc);
  }

  return OS.str();
}

std::string mkModuleEllipsisMarkdown(const ModuleInputs &Inputs,
                                     const OptionType *SubmoduleType) {
  std::ostringstream OS;
  OS << "## Additional Module Inputs\n\n";
  OS << "`...` keeps this lambda open to module arguments that are not listed "
        "explicitly.";
  OS << "\n\n## Provided Inputs\n\n";
  appendModuleInputList(OS, Inputs);

  if (SubmoduleType) {
    OptionDescription Desc;
    Desc.Type = *SubmoduleType;
    OS << "\n`config` and `options` are scoped to this submodule.\n\n"
       << mkOptionMarkdown(Desc);
  }

  return OS.str();
}

std::optional<Hover> hoverModuleInput(
    const ModuleInputContext &Context, llvm::StringRef Src,
    const ParentMapAnalysis &PM,
    const std::function<std::vector<ResolvedOptionInfo>(
        const std::vector<std::string> &)> &Resolve) {
  if (!Context.Lambda || !Context.RangeNode)
    return std::nullopt;

  std::optional<std::vector<std::string>> Scope =
      moduleInputScopeForLambda(*Context.Lambda, PM);
  if (!Scope)
    return std::nullopt;

  ModuleInputs Inputs = resolveModuleInputs(*Scope, Resolve);
  std::optional<OptionType> SubmoduleType =
      resolveSubmoduleTypeForLambda(*Context.Lambda, PM, Resolve);
  const bool IsTopLevel = isTopLevelLambda(*Context.Lambda, PM);
  if (!SubmoduleType && !IsTopLevel)
    return std::nullopt;

  std::string Docs;
  if (Context.InputKind == ModuleInputContext::Kind::Ellipsis) {
    Docs = mkModuleEllipsisMarkdown(Inputs,
                                    SubmoduleType ? &*SubmoduleType : nullptr);
  } else {
    if (!allModuleInputNames(Inputs).contains(Context.Name))
      return std::nullopt;
    Docs = mkModuleInputMarkdown(Context.Name, Inputs,
                                 SubmoduleType ? &*SubmoduleType : nullptr);
  }

  return Hover{
      .contents =
          MarkupContent{
              .kind = MarkupKind::Markdown,
              .value = std::move(Docs),
          },
      .range = toLSPRange(Src, Context.RangeNode->range()),
  };
}

/// \brief Provide package information, library information ... , from nixpkgs.
class NixpkgsHoverProvider {
  AttrSetClient &NixpkgsClient;

  /// \brief Make markdown documentation by package description
  ///
  /// FIXME: there are many markdown generation in language server.
  /// Maybe we can add structured generating first?
  static std::string mkMarkdown(const PackageDescription &Package) {
    std::ostringstream OS;
    // Make each field a new section

    if (Package.Name) {
      OS << "`" << *Package.Name << "`";
      OS << "\n";
    }

    // Make links to homepage.
    if (Package.Homepage) {
      OS << "[homepage](" << *Package.Homepage << ")";
      OS << "\n";
    }

    if (Package.Description) {
      OS << "## Description"
         << "\n\n";
      OS << *Package.Description;
      OS << "\n\n";

      if (Package.LongDescription) {
        OS << "\n\n";
        OS << *Package.LongDescription;
        OS << "\n\n";
      }
    }

    return OS.str();
  }

  /// \brief Make markdown including both package and value description
  static std::string mkMarkdown(const AttrPathInfoResponse &Info) {
    std::ostringstream OS;
    // Package section (if available)
    OS << mkMarkdown(Info.PackageDesc);

    // Value description section
    if (Info.ValueDesc) {
      const auto &VD = *Info.ValueDesc;
      if (!OS.str().empty())
        OS << "\n";
      if (!VD.Doc.empty()) {
        OS << VD.Doc << "\n\n";
      }
      if (VD.Arity != 0) {
        OS << "**Arity:** " << VD.Arity << "\n";
      }
      if (!VD.Args.empty()) {
        OS << "**Args:** ";
        for (size_t Idx = 0; Idx < VD.Args.size(); ++Idx) {
          OS << "`" << VD.Args[Idx] << "`";
          if (Idx + 1 < VD.Args.size())
            OS << ", ";
        }
        OS << "\n";
      }
    }

    return OS.str();
  }

public:
  NixpkgsHoverProvider(AttrSetClient &NixpkgsClient)
      : NixpkgsClient(NixpkgsClient) {}

  std::optional<std::string> resolveSelector(const nixd::Selector &Sel) {
    std::binary_semaphore Ready(0);
    std::optional<AttrPathInfoResponse> Info;
    auto OnReply = [&Ready, &Info](llvm::Expected<AttrPathInfoResponse> Resp) {
      if (Resp)
        Info = *Resp;
      else
        elog("nixpkgs provider: {0}", Resp.takeError());
      Ready.release();
    };
    NixpkgsClient.attrpathInfo(Sel, std::move(OnReply));
    Ready.acquire();

    if (!Info)
      return std::nullopt;

    return mkMarkdown(*Info);
  }
};

/// \brief Get nixpkgs hover info from a selector.
std::optional<Hover> hoverNixpkgsSelector(const Selector &Sel,
                                          const nixf::Node &N,
                                          const VariableLookupAnalysis &VLA,
                                          const ParentMapAnalysis &PM,
                                          AttrSetClient &NixpkgsClient,
                                          llvm::StringRef Src) {
  try {
    // Ask nixpkgs provider information about this selector.
    NixpkgsHoverProvider NHP(NixpkgsClient);
    if (std::optional<std::string> Doc = NHP.resolveSelector(Sel)) {
      return Hover{
          .contents =
              MarkupContent{
                  .kind = MarkupKind::Markdown,
                  .value = std::move(*Doc),
              },
          .range = toLSPRange(Src, N.range()),
      };
    }
  } catch (std::exception &E) {
    elog("hover/idiom: {0}", E.what());
  }
  return std::nullopt;
}

/// \brief Get hover info for ExprVar.
std::optional<Hover> hoverVar(const ExprVar &Var,
                              const VariableLookupAnalysis &VLA,
                              const ParentMapAnalysis &PM,
                              AttrSetClient &NixpkgsClient,
                              llvm::StringRef Src) {
  try {
    Selector Sel = idioms::mkVarSelector(Var, VLA, PM);
    return hoverNixpkgsSelector(Sel, Var, VLA, PM, NixpkgsClient, Src);
  } catch (std::exception &E) {
    elog("hover/idiom/selector: {0}", E.what());
  }
  return std::nullopt;
}

/// \brief Get hover info for ExprSelect.
std::optional<Hover> hoverSelect(const ExprSelect &Sel,
                                 const VariableLookupAnalysis &VLA,
                                 const ParentMapAnalysis &PM,
                                 AttrSetClient &NixpkgsClient,
                                 llvm::StringRef Src) {
  try {
    Selector S = idioms::mkSelector(Sel, VLA, PM);
    return hoverNixpkgsSelector(S, Sel, VLA, PM, NixpkgsClient, Src);
  } catch (std::exception &E) {
    elog("hover/idiom/selector: {0}", E.what());
  }
  return std::nullopt;
}

} // namespace

void Controller::onHover(const TextDocumentPositionParams &Params,
                         Callback<std::optional<Hover>> Reply) {
  using CheckTy = std::optional<Hover>;
  auto Action = [Reply = std::move(Reply),
                 File = std::string(Params.textDocument.uri.file()),
                 RawPos = Params.position, this]() mutable {
    return Reply([&]() -> llvm::Expected<CheckTy> {
      const auto TU = CheckDefault(getTU(File));
      const auto AST = CheckDefault(getAST(*TU));
      const auto Pos = nixf::Position{RawPos.line, RawPos.character};
      const auto &N = *CheckDefault(AST->descend({Pos, Pos}));

      const auto &VLA = *TU->variableLookup();
      const auto &PM = *TU->parentMap();

      if (std::optional<FlakeOutputInputContext> Context =
              findFlakeOutputInputContext(N, VLA, PM, File)) {
        std::string Docs;
        if (Context->InputKind == FlakeOutputInputContext::Kind::Ellipsis) {
          Docs = renderFlakeOutputEllipsisMarkdown(Context->Inputs);
        } else {
          auto It = std::find_if(
              Context->Inputs.begin(), Context->Inputs.end(),
              [&](const FlakeOutputInput &Input) {
                return Input.Name == Context->Name;
              });
          if (It != Context->Inputs.end())
            Docs = renderFlakeOutputInputMarkdown(Context->Name, *It);
        }
        if (!Docs.empty()) {
          return Hover{
              .contents =
                  MarkupContent{
                      .kind = MarkupKind::Markdown,
                      .value = std::move(Docs),
                  },
              .range = toLSPRange(TU->src(), Context->RangeNode->range()),
          };
        }
      }

      if (std::optional<ModuleInputContext> Context =
              findModuleInputContext(N, VLA, PM)) {
        auto Resolve = [&](const std::vector<std::string> &Scope) {
          std::vector<std::string> AdjustedScope = Scope;
          if (flake_schema::isFlakeFile(File) && Context->Lambda &&
              flake_schema::isInsideOutputsBody(*Context->Lambda, PM))
            AdjustedScope = flake_schema::outputsBodyScope(AdjustedScope);
          return resolveOptionInfosForFile(File, AdjustedScope);
        };
        if (std::optional<Hover> H =
                hoverModuleInput(*Context, TU->src(), PM, Resolve))
          return *H;
      }

      const auto &UpExpr = *CheckDefault(PM.upExpr(N));

      if (UpExpr.kind() == Node::NK_ExprAttrs) {
        auto Scope = std::vector<std::string>();
        const auto R = findAttrPathForOptions(N, PM, Scope);
        if (R == FindAttrPathResult::OK) {
          if (flake_schema::isFlakeFile(File) &&
              flake_schema::isInsideOutputsBody(N, PM))
            Scope = flake_schema::outputsBodyScope(Scope);
          auto Resolve = [&](const std::vector<std::string> &Path) {
            return resolveOptionInfosForFile(File, Path);
          };
          auto ResolveDerived = [&](const std::vector<std::string> &Path) {
            return resolveDerivedOptionInfosForFile(File, Path);
          };
          auto CompleteDerived = [&](const std::vector<std::string> &Path,
                                     const std::string &Prefix) {
            return completeDerivedOptionsForFile(File, Path, Prefix);
          };
          if (std::optional<OptionDescription> Desc =
                  resolveHoverOptionDescription(Scope, Resolve, ResolveDerived,
                                               CompleteDerived)) {
            std::string Docs = mkOptionMarkdown(*Desc);
            return Hover{
                .contents =
                    MarkupContent{
                        .kind = MarkupKind::Markdown,
                        .value = std::move(Docs),
                    },
                .range = toLSPRange(TU->src(), N.range()),
            };
          }
        }

        if (std::optional<OptionFieldHoverContext> Context =
                findOptionFieldHoverContext(N, PM)) {
          if (flake_schema::isFlakeFile(File) &&
              flake_schema::isInsideOutputsBody(N, PM))
            Context->Scope = flake_schema::outputsBodyScope(Context->Scope);
          if (std::optional<Hover> H = hoverOptionValueField(
                  N, TU->src(), Context->ValuePath,
                  resolveDerivedOptionInfosForFile(File, Context->Scope)))
            return *H;
        }
      }

      // Try to get hover info from nixpkgs.
      if (auto *Client = nixpkgsClient(); Client) {
        switch (UpExpr.kind()) {
        case Node::NK_ExprVar: {
          const auto &Var = static_cast<const ExprVar &>(UpExpr);
          if (auto H = hoverVar(Var, VLA, PM, *Client, TU->src()))
            return *H;
          break;
        }
        case Node::NK_ExprSelect: {
          const auto &Sel = static_cast<const ExprSelect &>(UpExpr);
          if (auto H = hoverSelect(Sel, VLA, PM, *Client, TU->src()))
            return *H;
          break;
        }
        default:
          break;
        }
      }

      return std::nullopt;
    }());
  };
  postToPool(std::move(Action));
}

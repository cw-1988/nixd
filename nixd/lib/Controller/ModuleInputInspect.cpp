#include "nixd/Controller/ModuleInputInspect.h"

#include <nixf/Basic/Nodes/Lambda.h>
#include <nixf/Basic/Nodes/Simple.h>
#include <nixf/Sema/VariableLookup.h>

#include <cctype>
#include <fstream>
#include <initializer_list>
#include <set>
#include <sstream>
#include <utility>
#include <unistd.h>

namespace nixd {

namespace {

std::optional<ModuleInputInspectContext>
moduleInputFromDefinitionSyntax(const nixf::Node &Syntax,
                                const nixf::ParentMapAnalysis &PM) {
  const nixf::Node *LambdaNode = nullptr;
  std::string Name;

  if (const nixf::Node *FormalNode = PM.upTo(Syntax, nixf::Node::NK_Formal)) {
    const auto &Formal = static_cast<const nixf::Formal &>(*FormalNode);
    if (Formal.isEllipsis() || !Formal.id() || Formal.id() != &Syntax)
      return std::nullopt;

    Name = Formal.id()->name();
    LambdaNode = PM.upTo(*FormalNode, nixf::Node::NK_ExprLambda);
  } else if (Syntax.kind() == nixf::Node::NK_Identifier) {
    const nixf::Node *ArgNode = PM.upTo(Syntax, nixf::Node::NK_LambdaArg);
    if (!ArgNode)
      return std::nullopt;

    const auto &Arg = static_cast<const nixf::LambdaArg &>(*ArgNode);
    if (!Arg.id() || Arg.id() != &Syntax)
      return std::nullopt;

    Name = Arg.id()->name();
    LambdaNode = PM.upTo(*ArgNode, nixf::Node::NK_ExprLambda);
  }

  if (!LambdaNode)
    return std::nullopt;

  const auto &Lambda = static_cast<const nixf::ExprLambda &>(*LambdaNode);
  if (std::optional<OptionValueContext> Context =
          findOptionValueContext(Lambda, PM, Lambda.lCur().position()))
    return ModuleInputInspectContext{.Input = std::move(Name),
                                     .Scope = std::move(Context->Scope)};

  const nixf::Node *Current = &Lambda;
  while (Current) {
    if (PM.isRoot(*Current))
      return ModuleInputInspectContext{.Input = std::move(Name)};
    const nixf::Node *Parent = PM.query(*Current);
    if (!Parent)
      return ModuleInputInspectContext{.Input = std::move(Name)};
    if (Parent->kind() != nixf::Node::NK_ExprParen)
      return std::nullopt;
    Current = Parent;
  }

  return std::nullopt;
}

std::vector<std::string> appendScope(std::vector<std::string> Scope,
                                     std::initializer_list<std::string> Suffix) {
  Scope.insert(Scope.end(), Suffix.begin(), Suffix.end());
  return Scope;
}

std::set<std::string>
valueAttrNameSet(const std::vector<ResolvedOptionInfo> &Infos) {
  std::set<std::string> Names;
  for (const ResolvedOptionInfo &Info : Infos)
    Names.insert(Info.Description.ValueAttrNames.begin(),
                 Info.Description.ValueAttrNames.end());
  return Names;
}

std::vector<std::string> resolveModuleInputSources(
    std::string_view Name, const std::vector<std::string> &Scope,
    const std::function<std::vector<ResolvedOptionInfo>(
        const std::vector<std::string> &)> &Resolve) {
  std::vector<std::string> Sources;
  if (Name == "config" || Name == "lib" || Name == "options" ||
      Name == "specialArgs")
    Sources.emplace_back("module system");

  std::set<std::string> ModuleArgs =
      valueAttrNameSet(Resolve(appendScope(Scope, {"_module", "args"})));
  if (ModuleArgs.contains(std::string(Name)))
    Sources.emplace_back("_module.args");

  std::set<std::string> SpecialArgs =
      valueAttrNameSet(Resolve(appendScope(Scope, {"_module", "specialArgs"})));
  if (SpecialArgs.contains(std::string(Name)))
    Sources.emplace_back("specialArgs");

  return Sources;
}

bool isKeyword(std::string_view S) {
  static const std::set<std::string_view> Keywords = {
      "assert", "else", "if", "in", "inherit", "let", "or", "rec", "then",
      "with"};
  return Keywords.contains(S);
}

bool isIdentifier(std::string_view S) {
  if (S.empty() || isKeyword(S))
    return false;
  auto IsFirst = [](char C) {
    return std::isalpha(static_cast<unsigned char>(C)) || C == '_';
  };
  auto IsRest = [](char C) {
    return std::isalnum(static_cast<unsigned char>(C)) || C == '_' ||
           C == '-' || C == '\'';
  };
  if (!IsFirst(S.front()))
    return false;
  for (char C : S.substr(1))
    if (!IsRest(C))
      return false;
  return true;
}

std::string quoteAttrName(std::string_view Name) {
  if (isIdentifier(Name))
    return std::string(Name);

  std::string Out = "\"";
  for (size_t I = 0; I < Name.size(); ++I) {
    char C = Name[I];
    if (C == '"' || C == '\\')
      Out.push_back('\\');
    if (C == '$' && I + 1 < Name.size() && Name[I + 1] == '{')
      Out.push_back('\\');
    Out.push_back(C);
  }
  Out.push_back('"');
  return Out;
}

std::string renderTypeSummary(const OptionField &Field) {
  if (!Field.Description || !Field.Description->Type)
    return "";
  const OptionType &Type = *Field.Description->Type;
  std::ostringstream OS;
  if (Type.Name)
    OS << *Type.Name;
  if (Type.Description) {
    if (OS.tellp() > 0)
      OS << " - ";
    OS << *Type.Description;
  }
  return OS.str();
}

bool shouldProbeChildren(const OptionField &Field) {
  if (!Field.Description || !Field.Description->Type)
    return true;
  const OptionType &Type = *Field.Description->Type;
  return !Type.NestedTypes.empty() || !Type.KnownSubOptions.empty();
}

struct InspectRenderBudget {
  int RemainingQueries = 160;
  int RemainingNodes = 500;
};

std::vector<ResolvedOptionField>
uniqueFields(std::vector<ResolvedOptionField> Fields) {
  std::vector<ResolvedOptionField> Result;
  std::set<std::string> Seen;
  for (ResolvedOptionField &Field : Fields) {
    if (Seen.insert(Field.Field.Name).second)
      Result.emplace_back(std::move(Field));
  }
  return Result;
}

void renderOptionTree(
    std::ostringstream &OS, const std::vector<std::string> &Scope,
    unsigned Indent, unsigned Depth, InspectRenderBudget &Budget,
    const std::function<std::vector<ResolvedOptionField>(
        const std::vector<std::string> &, const std::string &)> &Complete) {
  constexpr unsigned MaxDepth = 4;
  constexpr size_t MaxChildren = 40;

  if (Depth >= MaxDepth) {
    OS << std::string(Indent, ' ') << "# ... more nested attributes available\n";
    return;
  }
  if (Budget.RemainingQueries-- <= 0 || Budget.RemainingNodes-- <= 0) {
    OS << std::string(Indent, ' ') << "# ... inspection budget reached\n";
    return;
  }

  std::vector<ResolvedOptionField> Fields = uniqueFields(Complete(Scope, ""));
  size_t Written = 0;
  for (const ResolvedOptionField &Resolved : Fields) {
    if (Written >= MaxChildren) {
      OS << std::string(Indent, ' ') << "# ... " << (Fields.size() - Written)
         << " more attributes omitted\n";
      break;
    }

    const OptionField &Field = Resolved.Field;
    std::vector<std::string> ChildScope = Scope;
    ChildScope.emplace_back(Field.Name);

    std::vector<ResolvedOptionField> Children;
    if (Depth + 1 < MaxDepth && Budget.RemainingQueries > 0 &&
        shouldProbeChildren(Field))
      Children = uniqueFields(Complete(ChildScope, ""));

    OS << std::string(Indent, ' ') << quoteAttrName(Field.Name);
    if (!Children.empty()) {
      OS << " = {\n";
      renderOptionTree(OS, ChildScope, Indent + 2, Depth + 1, Budget,
                       Complete);
      OS << std::string(Indent, ' ') << "};\n";
    } else {
      OS << " = null;";
      std::string Summary = renderTypeSummary(Field);
      if (!Summary.empty())
        OS << " # " << Summary;
      OS << "\n";
    }
    ++Written;
  }
}

std::vector<std::string> uniqueNames(std::vector<std::string> Names) {
  std::vector<std::string> Result;
  std::set<std::string> Seen;
  for (std::string &Name : Names) {
    if (Seen.insert(Name).second)
      Result.emplace_back(std::move(Name));
  }
  return Result;
}

void renderAttrTree(
    std::ostringstream &OS, const std::vector<std::string> &Scope,
    unsigned Indent, unsigned Depth, InspectRenderBudget &Budget,
    const ModuleInputAttrCompleter &Complete) {
  constexpr unsigned MaxDepth = 3;
  constexpr size_t MaxChildren = 40;

  if (!Complete) {
    OS << std::string(Indent, ' ')
       << "# Attribute discovery is not available for this input.\n";
    return;
  }
  if (Depth >= MaxDepth) {
    OS << std::string(Indent, ' ') << "# ... more nested attributes available\n";
    return;
  }
  if (Budget.RemainingQueries-- <= 0 || Budget.RemainingNodes-- <= 0) {
    OS << std::string(Indent, ' ') << "# ... inspection budget reached\n";
    return;
  }

  std::vector<std::string> Names = uniqueNames(Complete(Scope, ""));
  if (Names.empty()) {
    OS << std::string(Indent, ' ') << "# No attributes discovered here.\n";
    return;
  }

  size_t Written = 0;
  for (const std::string &Name : Names) {
    if (Written >= MaxChildren) {
      OS << std::string(Indent, ' ') << "# ... " << (Names.size() - Written)
         << " more attributes omitted\n";
      break;
    }

    std::vector<std::string> ChildScope = Scope;
    ChildScope.emplace_back(Name);

    std::vector<std::string> Children;
    if (Depth + 1 < MaxDepth && Budget.RemainingQueries > 0)
      Children = uniqueNames(Complete(ChildScope, ""));

    OS << std::string(Indent, ' ') << quoteAttrName(Name);
    if (!Children.empty()) {
      OS << " = {\n";
      renderAttrTree(OS, ChildScope, Indent + 2, Depth + 1, Budget, Complete);
      OS << std::string(Indent, ' ') << "};\n";
    } else {
      OS << " = null;\n";
    }
    ++Written;
  }
}

std::string sanitizeFilePart(std::string_view S) {
  std::string Out;
  for (char C : S) {
    if (std::isalnum(static_cast<unsigned char>(C)) || C == '-' || C == '_')
      Out.push_back(C);
    else
      Out.push_back('-');
  }
  if (Out.empty())
    Out = "module-input";
  return Out;
}

} // namespace

std::optional<ModuleInputInspectContext>
findModuleInputInspectContext(const nixf::Node &N,
                              const nixf::VariableLookupAnalysis &VLA,
                              const nixf::ParentMapAnalysis &PM,
                              const std::function<std::vector<ResolvedOptionInfo>(
                                  const std::vector<std::string> &)> &Resolve) {
  auto ResolveContext = [&](ModuleInputInspectContext Context)
      -> std::optional<ModuleInputInspectContext> {
    Context.Sources = resolveModuleInputSources(Context.Input, Context.Scope,
                                                Resolve);
    if (Context.Sources.empty())
      return std::nullopt;
    return Context;
  };

  if (std::optional<ModuleInputInspectContext> Context =
          moduleInputFromDefinitionSyntax(N, PM))
    return ResolveContext(std::move(*Context));

  const nixf::Node *ExprNode = PM.upExpr(N);
  if (!ExprNode || ExprNode->kind() != nixf::Node::NK_ExprVar)
    return std::nullopt;

  const auto &Var = static_cast<const nixf::ExprVar &>(*ExprNode);
  auto Lookup = VLA.query(Var);
  if (!Lookup.Def || !Lookup.Def->syntax())
    return std::nullopt;

  if (std::optional<ModuleInputInspectContext> Context =
          moduleInputFromDefinitionSyntax(*Lookup.Def->syntax(), PM))
    return ResolveContext(std::move(*Context));

  return std::nullopt;
}

llvm::json::Array
moduleInputInspectScopeToJSON(const std::vector<std::string> &Scope) {
  llvm::json::Array Result;
  for (const std::string &Segment : Scope)
    Result.emplace_back(Segment);
  return Result;
}

llvm::json::Array
moduleInputInspectSourcesToJSON(const std::vector<std::string> &Sources) {
  llvm::json::Array Result;
  for (const std::string &Source : Sources)
    Result.emplace_back(Source);
  return Result;
}

std::optional<std::vector<std::string>>
moduleInputInspectScopeFromJSON(const llvm::json::Value &V) {
  const auto *Array = V.getAsArray();
  if (!Array)
    return std::nullopt;
  std::vector<std::string> Scope;
  Scope.reserve(Array->size());
  for (const llvm::json::Value &Item : *Array) {
    auto Segment = Item.getAsString();
    if (!Segment)
      return std::nullopt;
    Scope.emplace_back(*Segment);
  }
  return Scope;
}

std::string renderModuleInputInspectionDocument(
    std::string_view Input, const std::vector<std::string> &Scope,
    const std::vector<std::string> &Sources, std::string_view SourceFile,
    const ModuleInputOptionCompleter &CompleteOptions,
    const ModuleInputAttrCompleter &CompleteAttrs) {
  std::ostringstream OS;
  OS << "# Generated by nixd for inspection only.\n";
  OS << "# Source: " << SourceFile << "\n";
  OS << "# Module input: " << Input << "\n";
  if (!Sources.empty()) {
    OS << "# Provided by:";
    for (const std::string &Source : Sources)
      OS << " " << Source;
    OS << "\n";
  }
  if (!Scope.empty()) {
    OS << "# Option scope:";
    for (const std::string &Segment : Scope)
      OS << " " << Segment;
    OS << "\n";
  }
  OS << "\n{\n";
  OS << "  " << quoteAttrName(Input) << " = {\n";
  if (Input == "config" || Input == "options") {
    InspectRenderBudget Budget;
    renderOptionTree(OS, Scope, 4, 0, Budget, CompleteOptions);
  } else {
    if (CompleteAttrs) {
      InspectRenderBudget Budget;
      renderAttrTree(OS, {}, 4, 0, Budget, CompleteAttrs);
    } else {
      OS << "    # nixd knows this module input is provided";
      if (!Sources.empty()) {
        OS << " by ";
        for (size_t I = 0; I < Sources.size(); ++I) {
          if (I)
            OS << ", ";
          OS << Sources[I];
        }
      }
      OS << ".\n";
      OS << "    # Expanding arbitrary module argument values is not available "
            "yet.\n";
    }
  }
  OS << "  };\n";
  OS << "}\n";
  return OS.str();
}

std::filesystem::path
writeModuleInputInspectionFile(std::string_view Input,
                               std::string_view SourceFile,
                               std::string Content) {
  std::filesystem::path Dir =
      std::filesystem::temp_directory_path() / "nixd-inspect";
  std::filesystem::create_directories(Dir);

  std::string Base = std::filesystem::path(std::string(SourceFile)).stem();
  std::filesystem::path File =
      Dir / (sanitizeFilePart(Base) + "-" + sanitizeFilePart(Input) + "-" +
             std::to_string(getpid()) + ".nix");

  std::ofstream Out(File);
  Out << Content;
  return File;
}

} // namespace nixd

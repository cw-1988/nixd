#include "nixd/Controller/FlakeInputInspect.h"

#include "AST.h"
#include "Option/FlakeSchema.h"

#include <nixf/Basic/Nodes/Attrs.h>
#include <nixf/Basic/Nodes/Lambda.h>
#include <nixf/Basic/Nodes/Simple.h>
#include <nixf/Sema/VariableLookup.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <unistd.h>

namespace nixd {

namespace {

using LookupResultKind = nixf::VariableLookupAnalysis::LookupResultKind;

struct FlakeOutputInputSyntaxContext {
  FlakeOutputInputContext::Kind InputKind = FlakeOutputInputContext::Kind::Named;
  std::string Name;
  const nixf::ExprLambda *Lambda = nullptr;
  const nixf::Node *RangeNode = nullptr;
};

const nixf::Node *rootNode(const nixf::Node &N,
                           const nixf::ParentMapAnalysis &PM) {
  const nixf::Node *Current = &N;
  std::set<const nixf::Node *> Seen;
  while (Current && !PM.isRoot(*Current) && Seen.insert(Current).second) {
    const nixf::Node *Parent = PM.query(*Current);
    if (!Parent)
      break;
    Current = Parent;
  }
  return Current;
}

std::optional<std::vector<std::string>>
bindingPath(const nixf::Binding &Binding, const nixf::ParentMapAnalysis &PM) {
  if (Binding.path().names().empty())
    return std::nullopt;
  const auto &Last = Binding.path().names().back();
  if (!Last || !Last->isStatic())
    return std::nullopt;

  std::vector<std::string> Path;
  if (findAttrPath(*Last, PM, Path) != FindAttrPathResult::OK)
    return std::nullopt;
  return Path;
}

std::optional<std::string> literalStringValue(const nixf::Expr *Value) {
  if (!Value || Value->kind() != nixf::Node::NK_ExprString)
    return std::nullopt;

  const auto &String = static_cast<const nixf::ExprString &>(*Value);
  if (!String.isLiteral())
    return std::nullopt;
  return String.literal();
}

bool isTopLevelFlakeBinding(const nixf::Binding &Binding,
                            const nixf::ParentMapAnalysis &PM) {
  const nixf::Node *Current = &Binding;
  std::set<const nixf::Node *> Seen;
  while (Current && Seen.insert(Current).second) {
    const nixf::Node *Parent = PM.query(*Current);
    if (!Parent)
      return false;

    if (Parent->kind() == nixf::Node::NK_Binds) {
      Current = Parent;
      continue;
    }

    if (Parent->kind() == nixf::Node::NK_ExprAttrs) {
      if (PM.isRoot(*Parent))
        return true;
      Current = Parent;
      continue;
    }

    if (Parent->kind() == nixf::Node::NK_Binding &&
        static_cast<const nixf::Binding &>(*Parent).value().get() == Current) {
      Current = Parent;
      continue;
    }

    return false;
  }

  return false;
}

void collectFlakeInputBindings(
    const nixf::Node &N, const nixf::ParentMapAnalysis &PM,
    std::map<std::string, FlakeOutputInput> &Inputs) {
  if (N.kind() == nixf::Node::NK_Binding) {
    const auto &Binding = static_cast<const nixf::Binding &>(N);
    if (isTopLevelFlakeBinding(Binding, PM)) {
      if (std::optional<std::vector<std::string>> Path =
            bindingPath(Binding, PM)) {
        if (Path->size() >= 2 && (*Path)[0] == "inputs") {
          const std::string &Name = (*Path)[1];
          FlakeOutputInput &Input = Inputs[Name];
          Input.Name = Name;
          if (Path->size() == 3 && (*Path)[2] == "url")
            Input.URL = literalStringValue(Binding.value().get());
        }
      }
    }
  }

  for (const nixf::Node *Child : N.children()) {
    if (Child)
      collectFlakeInputBindings(*Child, PM, Inputs);
  }
}

const FlakeOutputInput *
findInputInfo(std::string_view Name,
              const std::vector<FlakeOutputInput> &Inputs) {
  auto It = std::find_if(Inputs.begin(), Inputs.end(),
                         [&](const FlakeOutputInput &Input) {
                           return Input.Name == Name;
                         });
  return It == Inputs.end() ? nullptr : &*It;
}

std::optional<FlakeOutputInputSyntaxContext>
flakeInputFromDefinitionSyntax(const nixf::Node &Syntax,
                               const nixf::ParentMapAnalysis &PM) {
  if (const nixf::Node *FormalNode =
          PM.upTo(Syntax, nixf::Node::NK_Formal)) {
    const auto &Formal = static_cast<const nixf::Formal &>(*FormalNode);
    const nixf::Node *LambdaNode =
        PM.upTo(*FormalNode, nixf::Node::NK_ExprLambda);
    if (!LambdaNode)
      return std::nullopt;

    const auto &Lambda = static_cast<const nixf::ExprLambda &>(*LambdaNode);
    if (!flake_schema::isInsideOutputsBody(Lambda, PM))
      return std::nullopt;

    if (Formal.isEllipsis()) {
      if (&Formal.ellipsis() != &Syntax && FormalNode != &Syntax)
        return std::nullopt;
      return FlakeOutputInputSyntaxContext{
          .InputKind = FlakeOutputInputContext::Kind::Ellipsis,
          .Lambda = &Lambda,
          .RangeNode = &Syntax,
      };
    }

    const nixf::Identifier *ID = Formal.id();
    if (!ID || ID != &Syntax)
      return std::nullopt;

    return FlakeOutputInputSyntaxContext{
        .InputKind = FlakeOutputInputContext::Kind::Named,
        .Name = ID->name(),
        .Lambda = &Lambda,
        .RangeNode = &Syntax,
    };
  }

  if (Syntax.kind() != nixf::Node::NK_Identifier)
    return std::nullopt;

  const nixf::Node *ArgNode = PM.upTo(Syntax, nixf::Node::NK_LambdaArg);
  if (!ArgNode)
    return std::nullopt;

  const auto &Arg = static_cast<const nixf::LambdaArg &>(*ArgNode);
  const nixf::Identifier *ID = Arg.id();
  if (!ID || ID != &Syntax)
    return std::nullopt;

  const nixf::Node *LambdaNode =
      PM.upTo(*ArgNode, nixf::Node::NK_ExprLambda);
  if (!LambdaNode)
    return std::nullopt;

  const auto &Lambda = static_cast<const nixf::ExprLambda &>(*LambdaNode);
  if (!flake_schema::isInsideOutputsBody(Lambda, PM))
    return std::nullopt;

  return FlakeOutputInputSyntaxContext{
      .InputKind = FlakeOutputInputContext::Kind::Named,
      .Name = ID->name(),
      .Lambda = &Lambda,
      .RangeNode = &Syntax,
  };
}

std::optional<FlakeOutputInputContext>
resolveSyntaxContext(FlakeOutputInputSyntaxContext Syntax,
                     const nixf::ParentMapAnalysis &PM) {
  if (!Syntax.Lambda || !Syntax.RangeNode)
    return std::nullopt;

  std::vector<FlakeOutputInput> Inputs =
      collectFlakeOutputInputs(*Syntax.Lambda, PM);

  if (Syntax.InputKind == FlakeOutputInputContext::Kind::Named &&
      !findInputInfo(Syntax.Name, Inputs))
    return std::nullopt;

  return FlakeOutputInputContext{
      .InputKind = Syntax.InputKind,
      .Name = std::move(Syntax.Name),
      .RangeNode = Syntax.RangeNode,
      .Inputs = std::move(Inputs),
  };
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
    Out = "flake-input";
  return Out;
}

std::string inputSource(const FlakeOutputInput &Input) {
  if (Input.IsSelf)
    return "flake evaluator";
  return "inputs." + Input.Name;
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

void appendURL(std::ostringstream &OS, const FlakeOutputInput &Input) {
  if (Input.URL)
    OS << "\n\nURL: `" << *Input.URL << "`.";
}

} // namespace

std::vector<FlakeOutputInput>
collectFlakeOutputInputs(const nixf::Node &N,
                         const nixf::ParentMapAnalysis &PM) {
  std::map<std::string, FlakeOutputInput> InputMap;
  InputMap.emplace("self", FlakeOutputInput{.Name = "self", .IsSelf = true});

  if (const nixf::Node *Root = rootNode(N, PM))
    collectFlakeInputBindings(*Root, PM, InputMap);

  std::vector<FlakeOutputInput> Inputs;
  Inputs.reserve(InputMap.size());
  for (auto &[Name, Input] : InputMap) {
    (void)Name;
    Inputs.emplace_back(std::move(Input));
  }
  std::stable_sort(Inputs.begin(), Inputs.end(),
                   [](const FlakeOutputInput &A,
                      const FlakeOutputInput &B) {
                     if (A.IsSelf != B.IsSelf)
                       return A.IsSelf;
                     return A.Name < B.Name;
                   });
  return Inputs;
}

std::optional<FlakeOutputInputContext>
findFlakeOutputInputContext(const nixf::Node &N,
                            const nixf::VariableLookupAnalysis &VLA,
                            const nixf::ParentMapAnalysis &PM,
                            std::string_view File) {
  if (!flake_schema::isFlakeFile(File))
    return std::nullopt;

  if (std::optional<FlakeOutputInputSyntaxContext> Context =
          flakeInputFromDefinitionSyntax(N, PM))
    return resolveSyntaxContext(std::move(*Context), PM);

  const nixf::Node *ExprNode = PM.upExpr(N);
  if (!ExprNode || ExprNode->kind() != nixf::Node::NK_ExprVar)
    return std::nullopt;

  const auto &Var = static_cast<const nixf::ExprVar &>(*ExprNode);
  auto Lookup = VLA.query(Var);
  if (Lookup.Kind == LookupResultKind::Undefined || !Lookup.Def ||
      !Lookup.Def->syntax())
    return std::nullopt;

  if (std::optional<FlakeOutputInputSyntaxContext> Context =
          flakeInputFromDefinitionSyntax(*Lookup.Def->syntax(), PM)) {
    Context->RangeNode = ExprNode;
    return resolveSyntaxContext(std::move(*Context), PM);
  }

  return std::nullopt;
}

std::string renderFlakeOutputInputMarkdown(std::string_view Name,
                                           const FlakeOutputInput &Input) {
  std::ostringstream OS;
  OS << "## Flake Output Input\n\n";
  OS << "`" << Name << "`\n\n";
  OS << "Provided by: `" << inputSource(Input) << "`.";
  appendURL(OS, Input);
  OS << "\n\nNix calls `outputs` with `self` plus one attribute for each "
        "top-level flake input. This value is the resolved flake input, not "
        "the NixOS module `pkgs` package set.";
  return OS.str();
}

std::string renderFlakeOutputEllipsisMarkdown(
    const std::vector<FlakeOutputInput> &Inputs) {
  std::ostringstream OS;
  OS << "## Additional Flake Output Inputs\n\n";
  OS << "`...` keeps this `outputs` lambda open to `self` and inputs that are "
        "not listed explicitly.";
  OS << "\n\n## Provided Inputs\n\n";
  for (const FlakeOutputInput &Input : Inputs) {
    OS << "- `" << Input.Name << "` (`" << inputSource(Input) << "`";
    if (Input.URL)
      OS << ", `" << *Input.URL << "`";
    OS << ")\n";
  }
  return OS.str();
}

std::string renderFlakeOutputInputInspectionDocument(
    const FlakeOutputInputContext &Context, std::string_view SourceFile) {
  std::ostringstream OS;
  OS << "# Generated by nixd for inspection only.\n";
  OS << "# Source: " << SourceFile << "\n";

  if (Context.InputKind == FlakeOutputInputContext::Kind::Ellipsis) {
    OS << "# Flake output inputs: ...\n";
    OS << "# Nix calls outputs with self and one attribute per top-level input.\n";
    OS << "\n{\n";
    for (const FlakeOutputInput &Input : Context.Inputs) {
      OS << "  " << quoteAttrName(Input.Name) << " = null; # "
         << inputSource(Input);
      if (Input.URL)
        OS << ", " << *Input.URL;
      OS << "\n";
    }
    OS << "}\n";
    return OS.str();
  }

  const FlakeOutputInput *Input = findInputInfo(Context.Name, Context.Inputs);
  OS << "# Flake output input: " << Context.Name << "\n";
  if (Input) {
    OS << "# Provided by: " << inputSource(*Input) << "\n";
    if (Input->URL)
      OS << "# URL: " << *Input->URL << "\n";
  }
  OS << "# Note: this is the resolved flake input, not the NixOS module pkgs "
        "package set.\n";
  OS << "\n{\n";
  OS << "  " << quoteAttrName(Context.Name) << " = {\n";
  if (Input && Input->IsSelf) {
    OS << "    # Always provided by the flake evaluator.\n";
    OS << "    # This refers to the current flake's own outputs.\n";
  } else {
    OS << "    # Derived from top-level inputs." << Context.Name << ".\n";
    OS << "    # Common attrs depend on the input flake; nixpkgs usually exposes "
          "lib and legacyPackages.${system}.\n";
  }
  OS << "  };\n";
  OS << "}\n";
  return OS.str();
}

std::filesystem::path
writeFlakeOutputInputInspectionFile(const FlakeOutputInputContext &Context,
                                    std::string_view SourceFile,
                                    std::string Content) {
  std::filesystem::path Dir =
      std::filesystem::temp_directory_path() / "nixd-inspect";
  std::filesystem::create_directories(Dir);

  std::string Base = std::filesystem::path(std::string(SourceFile)).stem();
  std::string Input =
      Context.InputKind == FlakeOutputInputContext::Kind::Ellipsis
          ? "outputs-inputs"
          : Context.Name;
  std::filesystem::path File =
      Dir / (sanitizeFilePart(Base) + "-flake-" + sanitizeFilePart(Input) +
             "-" + std::to_string(getpid()) + ".nix");

  std::ofstream Out(File);
  Out << Content;
  return File;
}

} // namespace nixd

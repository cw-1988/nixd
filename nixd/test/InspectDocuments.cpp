#include <gtest/gtest.h>

#include "Parser.h"

#include "nixd/Controller/FlakeInputInspect.h"
#include "nixd/Controller/ModuleInputInspect.h"

namespace {

using namespace nixd;
using namespace nixf;
using namespace std::string_view_literals;

void expectParses(std::string_view Src) {
  std::vector<Diagnostic> Diags;
  Parser P(Src, Diags);
  std::shared_ptr<Expr> AST = P.parse();

  ASSERT_TRUE(AST);
  if (!Diags.empty()) {
    ADD_FAILURE() << "source:\n" << Src;
    for (const Diagnostic &Diag : Diags)
      ADD_FAILURE() << Diag.sname() << ": " << Diag.format();
  }
  ASSERT_TRUE(Diags.empty());
}

TEST(InspectDocuments, ModuleInputNamedDocumentParses) {
  auto CompleteOptions = [](const std::vector<std::string> &Scope,
                            const std::string &Prefix) {
    (void)Scope;
    (void)Prefix;

    OptionDescription Desc;
    Desc.Type = OptionType{
        .Description = "signed integer",
        .Name = "int",
    };
    return std::vector<ResolvedOptionField>{
        ResolvedOptionField{
            .ProviderName = "nixos",
            .Field = OptionField{
                .Name = "retries",
                .Description = Desc,
            },
        },
    };
  };

  std::string Doc = renderModuleInputInspectionDocument(
      "config", {"services", "example"}, {"module system"},
      "/tmp/module-input-definition.nix", CompleteOptions);

  EXPECT_NE(Doc.find("{\n  config = {\n"), std::string::npos);
  expectParses(Doc);
}

TEST(InspectDocuments, FlakeInputNamedDocumentParses) {
  FlakeOutputInputContext Context;
  Context.InputKind = FlakeOutputInputContext::Kind::Named;
  Context.Name = "nixpkgs";
  Context.Inputs = {
      FlakeOutputInput{.Name = "self", .IsSelf = true},
      FlakeOutputInput{
          .Name = "nixpkgs",
          .URL = "github:NixOS/nixpkgs",
      },
  };

  std::string Doc = renderFlakeOutputInputInspectionDocument(
      Context, "/tmp/flake.nix");

  EXPECT_NE(Doc.find("{\n  nixpkgs = {\n"), std::string::npos);
  expectParses(Doc);
}

} // namespace

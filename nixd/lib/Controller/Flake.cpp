/// \file
/// \brief Implementation of the flake schema helpers.

#include "Flake.h"

#include <llvm/Support/Path.h>

#include <nixf/Basic/Nodes/Attrs.h>
#include <nixf/Basic/Nodes/Expr.h>
#include <nixf/Basic/Nodes/Simple.h>
#include <nixf/Sema/ParentMap.h>

#include <algorithm>
#include <array>

using namespace nixd;
using namespace nixf;

namespace {

constexpr llvm::StringRef FlakeFileName = "flake.nix";

/// Guards against a pathologically nested source; real flakes need one or two.
constexpr unsigned MaxRootPeelDepth = 16;

constexpr std::array<FlakeAttr, 4> KnownFlakeAttrs = {{
    {"description", "flake | string", "\"\"",
     "A one-line description of the flake, shown by `nix flake metadata`."},
    {"inputs", "flake | attribute set of flake references", "{ }",
     "The flakes this one depends on, resolved and pinned in `flake.lock`."},
    {"outputs", "flake | function", "{ self, ... }: { }",
     "A function from the resolved inputs to what this flake provides, such "
     "as `packages`, `devShells` and `nixosConfigurations`."},
    {"nixConfig", "flake | attribute set of Nix settings", "{ }",
     "Nix settings applied when this flake is evaluated, most often "
     "`extra-substituters` and `extra-trusted-public-keys`."},
}};

} // namespace

llvm::ArrayRef<FlakeAttr> nixd::knownFlakeAttrs() { return KnownFlakeAttrs; }

bool nixd::isKnownFlakeAttr(llvm::StringRef Name) {
  return std::any_of(KnownFlakeAttrs.begin(), KnownFlakeAttrs.end(),
                     [&](const FlakeAttr &Attr) { return Attr.Name == Name; });
}

bool nixd::isFlakeFile(lspserver::PathRef File) {
  return llvm::sys::path::filename(File) == FlakeFileName;
}

const ExprAttrs *nixd::flakeRootAttrs(const Node &Root) {
  const Node *Current = &Root;
  for (unsigned Depth = 0; Current && Depth < MaxRootPeelDepth; ++Depth) {
    switch (Current->kind()) {
    case Node::NK_ExprAttrs:
      return static_cast<const ExprAttrs *>(Current);
    case Node::NK_ExprParen:
      Current = static_cast<const ExprParen *>(Current)->expr();
      break;
    case Node::NK_ExprLet:
      Current = static_cast<const ExprLet *>(Current)->expr();
      break;
    default:
      return nullptr;
    }
  }
  return nullptr;
}

bool nixd::isInFlakeRootAttrs(const Node &N, const Node &Root,
                              const ParentMapAnalysis &PM) {
  const ExprAttrs *RootAttrs = flakeRootAttrs(Root);
  if (!RootAttrs)
    return false;
  return PM.upTo(N, Node::NK_ExprAttrs) == RootAttrs;
}

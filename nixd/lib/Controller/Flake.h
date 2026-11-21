/// \file
/// \brief The flake schema: what Nix accepts at the root of `flake.nix`.
#pragma once

#include "lspserver/Path.h"

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/StringRef.h>

namespace nixf {
class ExprAttrs;
class Node;
class ParentMapAnalysis;
} // namespace nixf

namespace nixd {

/// \brief A top-level attribute Nix recognizes in `flake.nix`.
struct FlakeAttr {
  llvm::StringRef Name;
  /// Shown as the completion item's detail.
  llvm::StringRef Detail;
  /// Placeholder value used when inserting the attribute.
  llvm::StringRef Example;
  /// Markdown shown as the completion item's documentation.
  llvm::StringRef Doc;
};

/// \brief The only attributes Nix reads from a flake.
///
/// Everything else at the root is ignored by the evaluator, so these are also
/// the only names worth completing there.
llvm::ArrayRef<FlakeAttr> knownFlakeAttrs();

bool isKnownFlakeAttr(llvm::StringRef Name);

/// \brief Whether \p File is a `flake.nix`.
bool isFlakeFile(lspserver::PathRef File);

/// \brief Peel `let ... in` and parens to reach the attrs a flake evaluates to.
///
/// `flake.nix` is an ordinary Nix expression that must evaluate to an attribute
/// set, so `let ... in { ... }` is legal. Returns null when the root is
/// something that cannot be inspected statically, such as a lambda.
const nixf::ExprAttrs *flakeRootAttrs(const nixf::Node &Root);

/// \brief Whether \p N sits directly in the root attribute set of \p Root.
///
/// Used to tell "the user is naming a flake attribute" apart from "the user is
/// somewhere inside `outputs`", where the flake schema no longer applies.
bool isInFlakeRootAttrs(const nixf::Node &N, const nixf::Node &Root,
                        const nixf::ParentMapAnalysis &PM);

} // namespace nixd

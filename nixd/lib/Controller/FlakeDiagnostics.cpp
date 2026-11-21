/// \file
/// \brief Implementation of `flake.nix` top-level attribute checking.
///
/// Nix recognizes exactly four attributes at the root of a flake; see Flake.h.
/// Anything else is ignored by the evaluator, which makes a typo (`ouputs`,
/// `nixconfig`) or a stray attribute that belongs under `outputs` silently do
/// nothing.

#include "FlakeDiagnostics.h"

#include "Flake.h"

#include <nixf/Basic/Nodes/Attrs.h>
#include <nixf/Basic/Nodes/Expr.h>

#include <memory>
#include <string>

using namespace nixd;
using namespace nixf;

namespace {

NixdDiagnostic makeUnknownFlakeAttrDiagnostic(const AttrName &Name) {
  return NixdDiagnostic{
      .Range = Name.range(),
      .Severity = NixdDiagnosticSeverity::Warning,
      .Code = "flake-unknown-attr",
      .Source = "nixd",
      .Message = "attribute `" + Name.staticName() +
                 "` is ignored by Nix, `flake.nix` only accepts "
                 "`description`, `inputs`, `outputs` and `nixConfig`",
      .Tags = {nixf::DiagnosticTag::Faded},
  };
}

/// Only the first name of an attribute path lives at flake scope:
/// `inputs.nixpkgs.url = ...` declares the root attribute `inputs`.
const AttrName *rootAttrName(const Binding &Bind) {
  const auto &Names = Bind.path().names();
  if (Names.empty())
    return nullptr;
  return Names.front().get();
}

void checkAttrName(const AttrName *Name,
                   std::vector<NixdDiagnostic> &Diagnostics) {
  // Dynamic names (`${x} = ...`) cannot be resolved statically.
  if (!Name || !Name->isStatic())
    return;
  if (isKnownFlakeAttr(Name->staticName()))
    return;
  Diagnostics.emplace_back(makeUnknownFlakeAttrDiagnostic(*Name));
}

} // namespace

std::vector<NixdDiagnostic>
nixd::collectFlakeDiagnostics(lspserver::PathRef File, const NixTU &TU) {
  std::vector<NixdDiagnostic> Diagnostics;
  if (!isFlakeFile(File))
    return Diagnostics;
  if (!TU.ast() || hasRecoverySyntaxError(TU))
    return Diagnostics;

  const ExprAttrs *Root = flakeRootAttrs(*TU.ast());
  if (!Root || !Root->binds())
    return Diagnostics;

  for (const std::shared_ptr<Node> &Bind : Root->binds()->bindings()) {
    if (!Bind)
      continue;
    switch (Bind->kind()) {
    case Node::NK_Binding:
      checkAttrName(rootAttrName(static_cast<const Binding &>(*Bind)),
                    Diagnostics);
      break;
    case Node::NK_Inherit:
      // `inherit description;` binds a root attribute just like `=` does.
      for (const auto &Name : static_cast<const Inherit &>(*Bind).names())
        checkAttrName(Name.get(), Diagnostics);
      break;
    default:
      break;
    }
  }
  return Diagnostics;
}

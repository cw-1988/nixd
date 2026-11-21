/// \file
/// \brief Diagnostics for the top level of `flake.nix`.
#pragma once

#include "nixd/Controller/NixTU.h"

#include "lspserver/Path.h"

#include <vector>

namespace nixd {

/// \brief Flag attributes at the root of `flake.nix` that Nix does not accept.
///
/// Only applies to files actually named `flake.nix`; every other file yields no
/// diagnostics. Purely syntactic, so unlike option diagnostics this does not
/// depend on any evaluator being ready.
std::vector<NixdDiagnostic> collectFlakeDiagnostics(lspserver::PathRef File,
                                                    const NixTU &TU);

} // namespace nixd

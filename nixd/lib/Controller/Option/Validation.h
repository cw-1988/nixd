#pragma once

#include "nixd/Controller/NixTU.h"
#include "nixd/Controller/Option.h"

#include <nixf/Basic/Nodes/Expr.h>
#include <nixf/Sema/ParentMap.h>
#include <nixf/Sema/VariableLookup.h>

#include <vector>

namespace nixd::option_diagnostics {

enum class SchemaMatch {
  Matches,
  Mismatches,
  Unknown,
};

struct ValidationResult {
  SchemaMatch Match = SchemaMatch::Unknown;
  std::vector<NixdDiagnostic> Diagnostics;
};

ValidationResult validateType(const OptionType &Type, const nixf::Expr &Value,
                              const std::vector<std::string> &Scope,
                              const nixf::ParentMapAnalysis &PM,
                              const nixf::VariableLookupAnalysis *VLA);

} // namespace nixd::option_diagnostics

#pragma once

#include "Controller/Option/Validation.h"

#include "nixd/Controller/Option.h"

#include <nixf/Basic/Nodes/Expr.h>

namespace nixd::option_diagnostics::validation {

SchemaMatch scalarKindMatch(const OptionType &Type, const nixf::Expr &Value,
                            OptionLiteralKind Actual);
SchemaMatch stringConstraintMatch(const OptionType &Type,
                                  const nixf::Expr &Value,
                                  OptionLiteralKind Actual);

bool hasPathConstraint(const OptionType &Type);
SchemaMatch pathConstraintMatch(const OptionType &Type,
                                const nixf::Expr &Value,
                                OptionLiteralKind Actual);

SchemaMatch packageMatch(const nixf::Expr &Value, OptionLiteralKind Actual);

} // namespace nixd::option_diagnostics::validation

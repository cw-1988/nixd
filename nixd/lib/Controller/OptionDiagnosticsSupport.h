#pragma once

#include "nixd/Controller/NixTU.h"
#include "nixd/Controller/Option.h"

#include <nixf/Basic/Nodes/Attrs.h>
#include <nixf/Basic/Nodes/Expr.h>
#include <nixf/Sema/ParentMap.h>
#include <nixf/Sema/VariableLookup.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nixd::option_diagnostics {

std::string toLowerCopy(std::string_view S);
std::string renderScope(const std::vector<std::string> &Scope);
std::string renderExpected(const OptionType &Type);

std::vector<std::string> appendScope(std::vector<std::string> Scope,
                                     std::string Name);

const nixf::Expr &stripParens(const nixf::Expr &Value);
const nixf::Expr &resolveStaticValue(const nixf::Expr &Value,
                                     const nixf::ParentMapAnalysis &PM,
                                     const nixf::VariableLookupAnalysis *VLA);

std::optional<std::string> literalString(const nixf::Expr &Value);
std::optional<bool> literalBool(const nixf::Expr &Value);
bool literalNull(const nixf::Expr &Value);
std::optional<std::string> pathLiteralText(const nixf::Expr &Value);

bool hasInheritBinding(const nixf::ExprAttrs &Attrs);
bool isLetDefinitionBinding(const nixf::Binding &Binding,
                            const nixf::ParentMapAnalysis &PM);

NixdDiagnostic makeTypeDiagnostic(const nixf::Expr &Value,
                                  const std::vector<std::string> &Scope,
                                  OptionLiteralKind Actual,
                                  const OptionType &Expected);
NixdDiagnostic makeUnknownDiagnostic(const nixf::Node &Key,
                                     const std::vector<std::string> &Scope);
NixdDiagnostic makeRequiredDiagnostic(const nixf::Expr &Value,
                                      const std::vector<std::string> &Scope);

} // namespace nixd::option_diagnostics

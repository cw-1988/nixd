#pragma once

#include <string>
#include <string_view>

namespace nixd::completion {

/// \brief Escape \p Text for use inside an LSP snippet placeholder.
///
/// `\`, `$` and `}` are the characters that would otherwise terminate or
/// reinterpret the surrounding `${1:...}`.
inline std::string escapeSnippet(std::string_view Text) {
  std::string Out;
  Out.reserve(Text.size());
  for (char C : Text) {
    if (C == '\\' || C == '$' || C == '}')
      Out.push_back('\\');
    Out.push_back(C);
  }
  return Out;
}

} // namespace nixd::completion

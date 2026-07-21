#include "Controller/Completion/Options.h"

#include "Controller/AST.h"

#include "lspserver/SourceCode.h"

#include <llvm/Support/Error.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace nixd;
using namespace nixf;

namespace nixd::completion::options::attr_path {

namespace {

bool isInsideComment(std::string_view Src) {
  enum class State { Normal, Line, Block } Current = State::Normal;
  for (size_t I = 0; I < Src.size(); ++I) {
    const char Ch = Src[I];
    switch (Current) {
    case State::Normal:
      if (Ch == '#') {
        Current = State::Line;
      } else if (Ch == '/' && I + 1 < Src.size() && Src[I + 1] == '*') {
        Current = State::Block;
        ++I;
      }
      break;
    case State::Line:
      if (Ch == '\n' || Ch == '\r')
        Current = State::Normal;
      break;
    case State::Block:
      if (Ch == '*' && I + 1 < Src.size() && Src[I + 1] == '/') {
        Current = State::Normal;
        ++I;
      }
      break;
    }
  }
  return Current != State::Normal;
}

bool isAttrNameChar(char Ch) {
  return std::isalnum(static_cast<unsigned char>(Ch)) || Ch == '_' ||
         Ch == '\'' || Ch == '-';
}

std::optional<std::string> fieldPrefix(const ExprAttrs &Attrs, Position Pos,
                                       std::string_view Src) {
  lspserver::Position LSPPos{.line = Pos.line(), .character = Pos.column()};
  llvm::Expected<size_t> Offset =
      lspserver::positionToOffset(Src, LSPPos, true);
  if (!Offset) {
    llvm::consumeError(Offset.takeError());
    return std::nullopt;
  }

  size_t LineBegin = Src.rfind('\n', *Offset == 0 ? 0 : *Offset - 1);
  LineBegin = LineBegin == std::string_view::npos ? 0 : LineBegin + 1;
  size_t PrefixBegin = *Offset;
  while (PrefixBegin > LineBegin && isAttrNameChar(Src[PrefixBegin - 1]))
    --PrefixBegin;

  if (!std::all_of(Src.begin() + LineBegin, Src.begin() + PrefixBegin,
                   [](char Ch) { return Ch == ' ' || Ch == '\t'; }))
    return std::nullopt;

  std::string Prefix(Src.substr(PrefixBegin, *Offset - PrefixBegin));
  if (!Prefix.empty() &&
      !(std::isalpha(static_cast<unsigned char>(Prefix.front())) ||
        Prefix.front() == '_'))
    return std::nullopt;

  size_t GapBegin = Attrs.lCur().offset();
  if (const Binds *Body = Attrs.binds()) {
    for (const auto &Binding : Body->bindings()) {
      const size_t Begin = Binding->lCur().offset();
      const size_t End = Binding->rCur().offset();
      // A non-empty standalone prefix can be part of a malformed binding that
      // swallowed the following line during parser recovery. For a blank
      // prefix, retain the stricter structural check.
      if (Prefix.empty() && Begin <= *Offset && *Offset <= End)
        return std::nullopt;
      if (End < PrefixBegin)
        GapBegin = std::max(GapBegin, End);
    }
  }

  if (*Offset < GapBegin || *Offset > Src.size())
    return std::nullopt;
  if (isInsideComment(Src.substr(GapBegin, PrefixBegin - GapBegin)))
    return std::nullopt;
  return Prefix;
}

} // namespace

std::optional<AttrPathCompleteParams> params(const Node &N,
                                             const ParentMapAnalysis &PM,
                                             Position Pos,
                                             std::string_view Src) {
  std::vector<std::string> Scope;
  using PathResult = FindAttrPathResult;
  auto R = findAttrPathForOptions(N, PM, Scope);
  if (R == PathResult::NotAttrPath) {
    const Node *Expr = PM.upTo(N, Node::NK_ExprAttrs);
    if (!Expr || Expr->kind() != Node::NK_ExprAttrs)
      return std::nullopt;

    const auto &Attrs = static_cast<const ExprAttrs &>(*Expr);
    std::optional<std::string> Prefix = fieldPrefix(Attrs, Pos, Src);
    if (!Prefix)
      return std::nullopt;

    R = findAttrSetValuePathForOptions(Attrs, PM, Scope);
    Scope.push_back(std::move(*Prefix));
  }
  if (R != PathResult::OK || Scope.empty())
    return std::nullopt;

  std::string Prefix = Scope.back();
  Scope.pop_back();
  return AttrPathCompleteParams{.Scope = std::move(Scope),
                                .Prefix = std::move(Prefix)};
}

} // namespace nixd::completion::options::attr_path

std::optional<AttrPathCompleteParams>
nixd::completion::optionAttrPathCompletionParams(const Node &N,
                                                 const ParentMapAnalysis &PM,
                                                 Position Pos,
                                                 std::string_view Src) {
  return options::attr_path::params(N, PM, Pos, Src);
}

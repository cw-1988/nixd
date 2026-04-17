#include "Controller/Completion/Context.h"

#include "lspserver/Protocol.h"
#include "lspserver/SourceCode.h"

#include <llvm/Support/Error.h>

#include <cstddef>

namespace {

bool isWhitespace(char C) {
  return C == ' ' || C == '\t' || C == '\n' || C == '\r';
}

} // namespace

const nixf::Node *nixd::completion::findCompletionNode(const nixf::Node &AST,
                                                       std::string_view Src,
                                                       nixf::Position Pos) {
  if (const nixf::Node *Desc = AST.descend({Pos, Pos}))
    return Desc;

  lspserver::Position LSPPos{.line = Pos.line(), .character = Pos.column()};
  llvm::Expected<size_t> Offset =
      lspserver::positionToOffset(Src, LSPPos, true);
  if (!Offset) {
    llvm::consumeError(Offset.takeError());
    return nullptr;
  }

  for (size_t I = *Offset; I > 0; --I) {
    const size_t Prev = I - 1;
    if (isWhitespace(Src[Prev]))
      continue;
    const lspserver::Position PrevPos = lspserver::offsetToPosition(Src, Prev);
    if (const nixf::Node *Desc =
            AST.descend({nixf::Position(PrevPos.line, PrevPos.character),
                         Pos}))
      return Desc;
  }
  return nullptr;
}

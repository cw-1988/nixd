/// \file
/// \brief Completion for the top level of `flake.nix`.

#include "Controller/Completion/Flake.h"

#include "Controller/Completion/Options.h"
#include "Controller/Completion/Snippet.h"
#include "Controller/Flake.h"

#include <string>
#include <utility>

using namespace nixd;
using namespace lspserver;

namespace {

constexpr CompletionItemKind FlakeAttrKind = CompletionItemKind::Constructor;

CompletionItem makeItem(const FlakeAttr &Attr, bool CompletionSnippets) {
  CompletionItem Item;
  Item.label = Attr.Name.str();
  Item.detail = Attr.Detail.str();
  Item.kind = FlakeAttrKind;
  Item.documentation = MarkupContent{
      .kind = MarkupKind::Markdown,
      .value = Attr.Doc.str(),
  };

  if (CompletionSnippets) {
    Item.insertTextFormat = InsertTextFormat::Snippet;
    Item.insertText = Attr.Name.str() +
                      " = ${1:" + completion::escapeSnippet(Attr.Example) +
                      "};";
  } else {
    Item.insertTextFormat = InsertTextFormat::PlainText;
    Item.insertText = Attr.Name.str() + " = " + Attr.Example.str() + ";";
  }
  return Item;
}

} // namespace

void nixd::completion::completeFlakeRootNames(
    llvm::StringRef Prefix, bool CompletionSnippets,
    std::vector<CompletionItem> &Items) {
  for (const FlakeAttr &Attr : knownFlakeAttrs()) {
    if (!Attr.Name.starts_with(Prefix))
      continue;
    addItem(Items, makeItem(Attr, CompletionSnippets));
  }
}

#include "Controller/Completion/Variables.h"

#include "Controller/AST.h"
#include "Controller/Completion/Nixpkgs.h"
#include "Controller/Completion/Options.h"
#include "lspserver/Logger.h"

#include <nixf/Sema/VariableLookup.h>

#include <cassert>
#include <exception>
#include <string>

using namespace nixd;
using namespace lspserver;

namespace {

class VLACompletionProvider {
  const nixf::VariableLookupAnalysis &VLA;

  static CompletionItemKind getCompletionItemKind(const nixf::Definition &Def) {
    if (Def.isBuiltin())
      return CompletionItemKind::Keyword;
    return CompletionItemKind::Variable;
  }

  void collectDef(std::vector<CompletionItem> &Items, const nixf::EnvNode *Env,
                  const std::string &Prefix) {
    if (!Env)
      return;
    collectDef(Items, Env->parent(), Prefix);
    for (const auto &[Name, Def] : Env->defs()) {
      if (Name.starts_with("__"))
        continue;
      assert(Def);
      if (Name.starts_with(Prefix)) {
        nixd::completion::addItem(Items,
                                  CompletionItem{
                                      .label = Name,
                                      .kind = getCompletionItemKind(*Def),
                                  });
      }
    }
  }

public:
  VLACompletionProvider(const nixf::VariableLookupAnalysis &VLA) : VLA(VLA) {}

  void complete(const nixf::ExprVar &Desc, std::vector<CompletionItem> &Items,
                const nixf::ParentMapAnalysis &PM) {
    std::string Prefix = Desc.id().name();
    collectDef(Items, upEnv(Desc, VLA, PM), Prefix);
  }
};

} // namespace

void nixd::completion::completeVarName(const nixf::VariableLookupAnalysis &VLA,
                                       const nixf::ParentMapAnalysis &PM,
                                       const nixf::ExprVar &N,
                                       AttrSetClient &Client,
                                       std::vector<CompletionItem> &List) {
#define DBGPREFIX "completion/var"
#define DBG DBGPREFIX ": "
  VLACompletionProvider VLAP(VLA);
  VLAP.complete(N, List, PM);

  try {
    Selector Sel = idioms::mkVarSelector(N, VLA, PM);
    if (Sel.empty())
      return;

    NixpkgsCompletionProvider NCP(Client);
    NCP.completePackages(attrPathCompleteParams(Sel, /*IsComplete=*/false),
                         List);
  } catch (ExceedSizeError &) {
    throw;
  } catch (std::exception &E) {
    return lspserver::log(DBG "skipped, reason: {0}", E.what());
  }

#undef DBG
#undef DBGPREFIX
}

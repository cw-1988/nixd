#include "Controller/Completion/Select.h"

#include "Controller/AST.h"
#include "Controller/Completion/Nixpkgs.h"
#include "Controller/Completion/Options.h"
#include "lspserver/Logger.h"

#include <nixf/Basic/Nodes/Expr.h>

#include <exception>

using namespace nixd;
using namespace lspserver;

void nixd::completion::completeSelect(const nixf::ExprSelect &Select,
                                      AttrSetClient &Client,
                                      const nixf::VariableLookupAnalysis &VLA,
                                      const nixf::ParentMapAnalysis &PM,
                                      bool IsComplete,
                                      std::vector<CompletionItem> &List) {
#define DBGPREFIX "completion/select"
#define DBG DBGPREFIX ": "
  const nixf::Expr &BaseExpr = Select.expr();

  if (BaseExpr.kind() != nixf::Node::NK_ExprVar)
    return;

  const auto &Var = static_cast<const nixf::ExprVar &>(BaseExpr);
  NixpkgsCompletionProvider NCP(Client);

  try {
    Selector Sel =
        idioms::mkSelector(Select, idioms::mkVarSelector(Var, VLA, PM));
    NCP.completePackages(attrPathCompleteParams(Sel, IsComplete), List);
  } catch (ExceedSizeError &) {
    throw;
  } catch (std::exception &E) {
    return lspserver::log(DBG "skipped, reason: {0}", E.what());
  }

#undef DBG
#undef DBGPREFIX
}

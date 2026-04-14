#include "nixf/Sema/ParentMap.h"

#include <unordered_set>

using namespace nixf;

void ParentMapAnalysis::dfs(const Node *N, const Node *Parent) {
  if (!N)
    return;
  ParentMap.insert({N, Parent});
  for (const Node *Ch : N->children())
    dfs(Ch, N);
}

const Node *ParentMapAnalysis::query(const Node &N) const {
  return ParentMap.contains(&N) ? ParentMap.at(&N) : nullptr;
}

const Node *ParentMapAnalysis::upExpr(const Node &N) const {
  const Node *Current = &N;
  std::unordered_set<const Node *> Seen;
  while (Current && Seen.insert(Current).second) {
    if (Expr::isExpr(Current->kind()))
      return Current;
    const Node *Up = query(*Current);
    if (!Up || isRoot(Up, *Current))
      return nullptr;
    Current = Up;
  }
  return nullptr;
}

const Node *ParentMapAnalysis::upTo(const Node &N, Node::NodeKind Kind) const {
  const Node *Current = &N;
  std::unordered_set<const Node *> Seen;
  while (Current && Seen.insert(Current).second) {
    if (Current->kind() == Kind)
      return Current;
    const Node *Up = query(*Current);
    if (!Up || isRoot(Up, *Current))
      return nullptr;
    Current = Up;
  }
  return nullptr;
}

void ParentMapAnalysis::runOnAST(const Node &Root) {
  // Special case. Root node has itself as "parent".
  dfs(&Root, &Root);
}

bool nixf::ParentMapAnalysis::isRoot(const Node *Up, const Node &N) {
  return Up == &N;
}

bool nixf::ParentMapAnalysis::isRoot(const Node &N) const {
  return isRoot(query(N), N);
}

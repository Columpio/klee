#include "ProofObligation.h"
#include "ExecutionState.h"

namespace klee {

void ProofObligation::addAsUnblocked(ExecutionState & state) {
  unblocked.insert(&state);
}

void ProofObligation::block(ExecutionState & state) {
  auto it = unblocked.find(&state);
  assert(it != unblocked.end());
  unblocked.erase(it);
  blocking_locs.insert(state.getInitPCBlock());
}

void ProofObligation::unblockTree(ProofObligation & node) {
  for (auto child : node.children) {
    unblock(node);
    unblockTree(node);
    node.answered = true;
  }
}

void ProofObligation::unblock(ProofObligation & node) {
  // for (auto state : node.unblocked)
  //   state->unblock(node);
  node.unblocked.clear();
}

} // namespace klee

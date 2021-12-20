#include "Initializer.h"
#include "ProofObligation.h"
#include <utility>

namespace klee {

bool SDInitializer::empty() {
  for(auto i : pobs) {
    auto distmap = i->parent->getBackwardDistance(i);
    for(auto j: distmap) {
      if(initializedLocs.count(j.first)) continue;
      return false;
    }
    auto fdistmap = i->parent->parent->getBackwardDistance(i->parent);
    for(auto j : fdistmap) {
      if(initializedLocs.count(j.first->entryKBlock)) continue;
      return false;
    }
  }
  return true;
}

std::pair<KBlock*, std::unordered_set<KBlock*>> SDInitializer::selectAction() {
  for(auto i : pobs) {
    auto distmap = i->parent->getBackwardDistance(i);
    for(auto j: distmap) {
      if(initializedLocs.count(j.first)) continue;
      initializedLocs.insert(j.first);
      return std::make_pair(j.first, pobs);
    }
    auto fdistmap = i->parent->parent->getBackwardDistance(i->parent);
    for(auto j : fdistmap) {
      if(initializedLocs.count(j.first->entryKBlock)) continue;
      initializedLocs.insert(j.first->entryKBlock);
      return std::make_pair(j.first->entryKBlock, pobs);
    }
  }
  assert(0);
}

void SDInitializer::addPob(ProofObligation* pob) {
  pobs.insert(pob->location);
}

};

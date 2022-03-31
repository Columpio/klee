//===-- BidirectionalSearcher.h ----------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "BidirectionalSearcher.h"
#include "BackwardSearcher.h"
#include "Executor.h"
#include "ExecutionState.h"
#include "ForwardSearcher.h"
#include "Initializer.h"
#include "MergeHandler.h"
#include "ProofObligation.h"
#include "SearcherUtil.h"
#include "UserSearcher.h"
#include "klee/Core/Interpreter.h"
#include "klee/Module/KModule.h"
#include "klee/Support/ErrorHandling.h"
#include <llvm/ADT/StringExtras.h>
#include <memory>
#include <unordered_set>
#include <variant>
#include <vector>

#include <cstdlib>

namespace klee {

Action &ForwardBidirectionalSearcher::selectAction() {
  while(!searcher->empty()) {
    auto &state = searcher->selectState();
    KInstruction *prevKI = state.prevPC;

    if (prevKI->inst->isTerminator() &&
        state.targets.empty() &&
        state.multilevel.count(state.getPCBlock()) > 0 /* maxcycles - 1 */) {
      KBlock *target = ex->calculateTargetByTransitionHistory(state);
      if (target) {
        state.targets.insert(target);
        ex->updateStates(&state);
        return *(new ForwardAction(&state));
      } else {
        ex->pauseState(state);
        ex->updateStates(nullptr);
      }
    } else {
      return *(new ForwardAction(&state));
    }
  }
  return *(new TerminateAction());
}

void ForwardBidirectionalSearcher::update(ActionResult r) {
  if(std::holds_alternative<ForwardResult>(r)) {
    auto fr = std::get<ForwardResult>(r);
    searcher->update(fr.current, fr.addedStates, fr.removedStates);
  }
}

ForwardBidirectionalSearcher::ForwardBidirectionalSearcher(SearcherConfig cfg) {
  searcher = new GuidedSearcher(
      constructUserSearcher(*(Executor *)(cfg.executor)), true);
  for(auto target : cfg.targets) {
    cfg.initial_state->targets.insert(target);
  }
  searcher->update(nullptr, {cfg.initial_state}, {});
  ex = cfg.executor;
}

void ForwardBidirectionalSearcher::removeProofObligation(ProofObligation* pob) {}

bool ForwardBidirectionalSearcher::empty() {
  return searcher->empty();
}

BidirectionalSearcher::StepKind
BidirectionalSearcher::selectStep() {
  unsigned int tick = choice;
  if (empty())
    return StepKind::Terminate;
  do {
    unsigned int i = choice;
    choice = (choice + 1) % 4;
    if (i == 0 && !forward->empty())
      return StepKind::Forward;
    else if (i == 1 && !branch->empty())
      return StepKind::Branch;
    else if (i == 2 && !backward->empty())
      return StepKind::Backward;
    else if (i == 3 && !initializer->empty())
      return StepKind::Initialize;
  } while (tick != choice);

  return StepKind::Terminate;
}

Action &BidirectionalSearcher::selectAction() {
  Action *action = nullptr;
  while (!action) {
    switch (selectStep()) {

    case StepKind::Forward: {
      auto &state = forward->selectState();
      KInstruction *prevKI = state.prevPC;
      if (prevKI->inst->isTerminator() &&
          state.targets.empty() &&
          state.multilevel.count(state.getPCBlock()) > 0 /* maxcycles - 1 */) {
        KBlock *target = ex->calculateTargetByTransitionHistory(state);
        if (target) {
          state.targets.insert(target);
          ex->updateStates(&state);
          action = new ForwardAction(&state);
        } else {
          ex->pauseState(state);
          ex->updateStates(nullptr);
        }
      } else
        action = new ForwardAction(&state);
      break;
    }

    case StepKind::Branch : {
      auto& state = branch->selectState();
      action = new ForwardAction(&state);
      break;
    }

    case StepKind::Backward : {
      auto pobState = backward->selectAction();
      action = new BackwardAction(pobState.second, pobState.first);
      break;
    }

    case StepKind::Initialize : {
      auto initAndTargets = initializer->selectAction();
      action = new InitializeAction(
        initAndTargets.first,
        initAndTargets.second);
      break;
    }

    case StepKind::Terminate : {
      action = new TerminateAction();
      break;
    }

    }
  }
  return *action;
}

void BidirectionalSearcher::update(ActionResult r) {
  if(std::holds_alternative<ForwardResult>(r)) {
    auto fr = std::get<ForwardResult>(r);
    ExecutionState* fwdCur = nullptr, *brnchCur = nullptr;
    std::vector<ExecutionState*> fwdAdded;
    std::vector<ExecutionState*> brnchAdded;
    std::vector<ExecutionState*> fwdRemoved;
    std::vector<ExecutionState*> brnchRemoved;

    if(fr.current) {
      if(fr.current->isIsolated())
        brnchCur = fr.current;
      else
        fwdCur = fr.current;
    }
    for(auto i : fr.addedStates) {
      if(i->isIsolated())
        brnchAdded.push_back(i);
      else fwdAdded.push_back(i);
    }
    for(auto i : fr.removedStates) {
      if(i->isIsolated())
        brnchRemoved.push_back(i);
      else fwdRemoved.push_back(i);
    }

    branch->update(brnchCur, brnchAdded, brnchRemoved);
    auto reached = branch->collectAndClearReached();
    for(auto i : reached) {
      if (ex->initialState->getInitPCBlock() == i->getInitPCBlock() || i->maxLevel == 1) {
        ex->emanager->states[i->pc->parent->basicBlock].insert(i->copy());
      }
    }

    forward->update(fwdCur, fwdAdded, fwdRemoved);

    if(fr.current && fr.validityCoreInit.first) {
      initializer->addValidityCoreInit(fr.validityCoreInit);
      if (mainLocs.count(fr.validityCoreInit.second->basicBlock) == 0) {
        mainLocs.insert(fr.validityCoreInit.second->basicBlock);
        ProofObligation* pob = new ProofObligation(fr.validityCoreInit.second, 0, 0);
        backward->update(pob);
      }
    }

  } else if (std::holds_alternative<BackwardResult>(r)) {
    auto br = std::get<BackwardResult>(r);
    if (br.newPob) {
      backward->update(br.newPob);
      initializer->addPob(br.newPob);
    }
  } else {
    auto ir = std::get<InitializeResult>(r);
    branch->update(nullptr, {ir.state}, {});
  }
}

BidirectionalSearcher::BidirectionalSearcher(SearcherConfig cfg) {
  ex = cfg.executor;
  forward = std::unique_ptr<ForwardSearcher>(
    new GuidedSearcher(std::unique_ptr<ForwardSearcher>(new BFSSearcher()), true));
  for(auto target : cfg.targets) {
    cfg.initial_state->targets.insert(target);
  }
  forward->update(nullptr, {cfg.initial_state}, {});
  branch = new GuidedSearcher(std::unique_ptr<ForwardSearcher>(new BFSSearcher()), false);
  backward = new BFSBackwardSearcher(cfg.executor->getExecutionManager(), cfg.targets);
  initializer = new ValidityCoreInitializer(cfg.targets);
}

void BidirectionalSearcher::removeProofObligation(ProofObligation* pob) {
  while (pob->parent) {
    pob = pob->parent;
  }
  removePob(pob);
  delete pob;
}

void BidirectionalSearcher::removePob(ProofObligation* pob) {
  backward->removePob(pob);
  for (auto child : pob->children) {
    removePob(child);
  }
}

bool BidirectionalSearcher::empty() {
  return forward->empty() && backward->empty() && initializer->empty(); // && branch->empty()
}

} // namespace klee

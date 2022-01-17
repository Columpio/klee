//===-- BidirectionalSearcher.h ---------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#pragma once
#include "BackwardSearcher.h"
#include "Executor.h"
#include "Initializer.h"
#include "SearcherUtil.h"
#include "ProofObligation.h"
#include "ForwardSearcher.h"
#include "klee/Module/KModule.h"
#include <memory>
#include <unordered_set>
#include <vector>
#include <variant>


namespace klee {

class IBidirectionalSearcher {
public:

  virtual Action selectAction() = 0;
  virtual void update(ActionResult) = 0;
  
};

class ForwardBidirSearcher : public IBidirectionalSearcher {
public:
  Action selectAction() override;
  void update(ActionResult) override;

  explicit ForwardBidirSearcher(SearcherConfig);
  
private:
  GuidedForwardSearcher* searcher;
  Executor* ex; // hack
};

class BidirectionalSearcher : public IBidirectionalSearcher {
public:
  Action selectAction() override;
  void update(ActionResult) override;

  explicit BidirectionalSearcher(SearcherConfig);
  
private:
  
  Executor* ex; // hack
  GuidedForwardSearcher* forward;
  GuidedForwardSearcher* branch;
  BFSBackwardSearcher* backward;
  ForkInitializer* initializer;
};

} // namespace klee

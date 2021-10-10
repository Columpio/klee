//===-- BidirectionalSearcher.h ----------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "BidirectionalSearcher.h"

namespace klee {

Action BidirectionalSearcher::selectState() {
  // static_assert(false && "TODO");
  return {Action::Type::Init, nullptr, nullptr, nullptr};
}

bool BidirectionalSearcher::empty() {
  // static_assert(false && "TODO");
  return true;
}

} // klee namespace

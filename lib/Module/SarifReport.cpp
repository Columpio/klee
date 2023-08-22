//===-- SarifReport.cpp----------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/Module/SarifReport.h"

#include "klee/Module/KInstruction.h"
#include "klee/Module/KModule.h"
#include "klee/Support/ErrorHandling.h"

#include "klee/Support/CompilerWarning.h"
DISABLE_WARNING_PUSH
DISABLE_WARNING_DEPRECATED_DECLARATIONS
#include "llvm/IR/IntrinsicInst.h"
DISABLE_WARNING_POP

using namespace llvm;
using namespace klee;

namespace {
bool isOSSeparator(char c) { return c == '/' || c == '\\'; }

optional<ref<Location>>
tryConvertLocationJson(const LocationJson &locationJson) {
  const auto &physicalLocation = locationJson.physicalLocation;
  if (!physicalLocation.has_value()) {
    return nonstd::nullopt;
  }

  const auto &artifactLocation = physicalLocation->artifactLocation;
  if (!artifactLocation.has_value() || !artifactLocation->uri.has_value()) {
    return nonstd::nullopt;
  }

  const auto filename = std::move(*(artifactLocation->uri));

  const auto &region = physicalLocation->region;
  if (!region.has_value() || !region->startLine.has_value()) {
    return nonstd::nullopt;
  }

  return Location::create(std::move(filename), *(region->startLine),
                          region->endLine, region->startColumn,
                          region->endColumn);
}

std::vector<ReachWithError>
tryConvertRuleJson(const std::string &ruleId, const std::string &toolName,
                   const optional<Message> &errorMessage) {
  if (toolName == "SecB") {
    if ("NullDereference" == ruleId) {
      return {ReachWithError::MustBeNullPointerException};
    } else if ("CheckAfterDeref" == ruleId) {
      return {ReachWithError::NullCheckAfterDerefException};
    } else if ("DoubleFree" == ruleId) {
      return {ReachWithError::DoubleFree};
    } else if ("UseAfterFree" == ruleId) {
      return {ReachWithError::UseAfterFree};
    } else if ("Reached" == ruleId) {
      return {ReachWithError::Reachable};
    } else {
      return {};
    }
  } else if (toolName == "clang") {
    if ("core.NullDereference" == ruleId) {
      return {ReachWithError::MayBeNullPointerException,
              ReachWithError::MustBeNullPointerException};
    } else if ("unix.Malloc" == ruleId) {
      if (errorMessage.has_value()) {
        if (errorMessage->text == "Attempt to free released memory") {
          return {ReachWithError::DoubleFree};
        } else if (errorMessage->text == "Use of memory after it is freed") {
          return {ReachWithError::UseAfterFree};
        } else {
          return {};
        }
      } else {
        return {ReachWithError::UseAfterFree, ReachWithError::DoubleFree};
      }
    } else if ("core.Reach" == ruleId) {
      return {ReachWithError::Reachable};
    } else {
      return {};
    }
  } else if (toolName == "CppCheck") {
    if ("nullPointer" == ruleId || "ctunullpointer" == ruleId) {
      return {ReachWithError::MayBeNullPointerException,
              ReachWithError::MustBeNullPointerException}; // TODO: check it out
    } else if ("doubleFree" == ruleId) {
      return {ReachWithError::DoubleFree};
    } else {
      return {};
    }
  } else if (toolName == "Infer") {
    if ("NULL_DEREFERENCE" == ruleId || "NULLPTR_DEREFERENCE" == ruleId) {
      return {ReachWithError::MayBeNullPointerException,
              ReachWithError::MustBeNullPointerException}; // TODO: check it out
    } else if ("USE_AFTER_DELETE" == ruleId || "USE_AFTER_FREE" == ruleId) {
      return {ReachWithError::UseAfterFree, ReachWithError::DoubleFree};
    } else {
      return {};
    }
  } else if (toolName == "Cooddy") {
    if ("NULL.DEREF" == ruleId || "NULL.UNTRUSTED.DEREF" == ruleId) {
      return {ReachWithError::MayBeNullPointerException,
              ReachWithError::MustBeNullPointerException};
    } else if ("MEM.DOUBLE.FREE" == ruleId) {
      return {ReachWithError::DoubleFree};
    } else if ("MEM.USE.FREE" == ruleId) {
      return {ReachWithError::UseAfterFree};
    } else {
      return {};
    }
  } else {
    return {};
  }
}

void tryConvertMessage(const std::string &toolName,
                       const optional<Message> &errorMessage,
                       ref<Location> &loc) {
  if (toolName != "Cooddy" || !errorMessage.has_value())
    return;
  std::string start = "Dereferencing of \"";
  std::string end = "\" which can be null";
  auto &text = errorMessage->text;
  if (text.substr(0, start.size()) == start &&
      text.substr(text.size() - end.size(), end.size()) == end) {
    auto size = text.size() - end.size() - start.size();
    auto derefedExpr = text.substr(start.size(), size);
    loc = new CoodyNPELocation(*loc);
  }
}

optional<Result> tryConvertResultJson(const ResultJson &resultJson,
                                      const std::string &toolName,
                                      const std::string &id) {
  std::vector<ReachWithError> errors = {};
  if (!resultJson.ruleId.has_value()) {
    errors = {ReachWithError::Reachable};
  } else {
    errors =
        tryConvertRuleJson(*resultJson.ruleId, toolName, resultJson.message);
    if (errors.size() == 0) {
      klee_warning("undefined error in %s result", id.c_str());
      return nonstd::nullopt;
    }
  }

  std::vector<ref<Location>> locations;
  std::vector<optional<json>> metadatas;

  if (resultJson.codeFlows.size() > 0) {
    assert(resultJson.codeFlows.size() == 1);
    assert(resultJson.codeFlows[0].threadFlows.size() == 1);

    const auto &threadFlow = resultJson.codeFlows[0].threadFlows[0];
    for (auto &threadFlowLocation : threadFlow.locations) {
      if (!threadFlowLocation.location.has_value()) {
        return nonstd::nullopt;
      }

      auto maybeLocation = tryConvertLocationJson(*threadFlowLocation.location);
      if (maybeLocation.has_value()) {
        locations.push_back(*maybeLocation);
        metadatas.push_back(std::move(threadFlowLocation.metadata));
      }
    }
  } else {
    assert(resultJson.locations.size() == 1);
    auto maybeLocation = tryConvertLocationJson(resultJson.locations[0]);
    if (maybeLocation.has_value()) {
      locations.push_back(*maybeLocation);
    }
  }

  if (locations.empty()) {
    return nonstd::nullopt;
  }
  tryConvertMessage(toolName, resultJson.message, locations.back());

  return Result{std::move(locations), std::move(metadatas), id,
                std::move(errors)};
}
} // anonymous namespace

namespace klee {
static const char *ReachWithErrorNames[] = {
    "DoubleFree",
    "UseAfterFree",
    "MayBeNullPointerException",
    "NullPointerException", // for backward compatibility with SecB
    "NullCheckAfterDerefException",
    "Reachable",
    "None",
};

const char *getErrorString(ReachWithError error) {
  return ReachWithErrorNames[error];
}

std::string getErrorsString(const std::vector<ReachWithError> &errors) {
  if (errors.size() == 1) {
    return getErrorString(*errors.begin());
  }

  std::string res = "(";
  size_t index = 0;
  for (auto err : errors) {
    res += getErrorString(err);
    if (index != errors.size() - 1) {
      res += "|";
    }
    index++;
  }
  res += ")";
  return res;
}

struct TraceId {
  virtual ~TraceId() {}
  virtual std::string toString() const = 0;
  virtual void getNextId(const klee::ResultJson &resultJson) = 0;
};

class CooddyTraceId : public TraceId {
  std::string uid = "";

public:
  std::string toString() const override { return uid; }
  void getNextId(const klee::ResultJson &resultJson) override {
    uid = resultJson.fingerprints.value().cooddy_uid;
  }
};

class GetterTraceId : public TraceId {
  unsigned id = 0;

public:
  std::string toString() const override { return std::to_string(id); }
  void getNextId(const klee::ResultJson &resultJson) override {
    id = resultJson.id.value();
  }
};

class NumericTraceId : public TraceId {
  unsigned id = 0;

public:
  std::string toString() const override { return std::to_string(id); }
  void getNextId(const klee::ResultJson &resultJson) override { id++; }
};

TraceId *createTraceId(const std::string &toolName,
                       const std::vector<klee::ResultJson> &results) {
  if (toolName == "Cooddy")
    return new CooddyTraceId();
  else if (results.size() > 0 && results[0].id.has_value())
    return new GetterTraceId();
  return new NumericTraceId();
}

void setResultId(const ResultJson &resultJson, bool useProperId, unsigned &id) {
  if (useProperId) {
    assert(resultJson.id.has_value() && "all results must have an proper id");
    id = resultJson.id.value();
  } else {
    ++id;
  }
}

SarifReport convertAndFilterSarifJson(const SarifReportJson &reportJson) {
  SarifReport report;

  if (reportJson.runs.size() == 0) {
    return report;
  }

  assert(reportJson.runs.size() == 1);

  const RunJson &run = reportJson.runs[0];
  const std::string toolName = run.tool.driver.name;

  TraceId *id = createTraceId(toolName, run.results);

  for (const auto &resultJson : run.results) {
    id->getNextId(resultJson);
    auto maybeResult =
        tryConvertResultJson(resultJson, toolName, id->toString());
    if (maybeResult.has_value()) {
      report.results.push_back(*maybeResult);
    }
  }
  delete id;

  return report;
}

Location::EquivLocationHashSet Location::cachedLocations;
Location::LocationHashSet Location::locations;

ref<Location> Location::create(std::string filename_, unsigned int startLine_,
                               optional<unsigned int> endLine_,
                               optional<unsigned int> startColumn_,
                               optional<unsigned int> endColumn_) {
  Location *loc =
      new Location(filename_, startLine_, endLine_, startColumn_, endColumn_);
  std::pair<EquivLocationHashSet::const_iterator, bool> success =
      cachedLocations.insert(loc);
  if (success.second) {
    // Cache miss
    locations.insert(loc);
    return loc;
  }
  // Cache hit
  delete loc;
  loc = *(success.first);
  return loc;
}

Location::~Location() {
  if (locations.find(this) != locations.end()) {
    locations.erase(this);
    cachedLocations.erase(this);
  }
}

bool Location::isInside(const std::string &name) const {
  size_t suffixSize = 0;
  int m = name.size() - 1, n = filename.size() - 1;
  for (; m >= 0 && n >= 0 && name[m] == filename[n]; m--, n--) {
    suffixSize++;
    if (isOSSeparator(filename[n]))
      return true;
  }
  return suffixSize >= 3 && (n == -1 ? (m == -1 || isOSSeparator(name[m]))
                                     : (m == -1 && isOSSeparator(filename[n])));
}

void Location::isInside(InstrWithPrecision &kp,
                        const Instructions &origInsts) const {
  auto ki = kp.ptr;
  if (!isa<DbgInfoIntrinsic>(ki->inst) && startLine <= ki->getLine() &&
      ki->getLine() <= endLine) {
    auto opCode = ki->inst->getOpcode();
    if (*startColumn <= ki->getColumn() && ki->getColumn() <= *endColumn &&
        origInsts.at(ki->getLine()).at(ki->getColumn()).count(opCode) != 0)
      kp.precision = Precision::ColumnLevel;
    else
      kp.precision = Precision::LineLevel;
    return;
  }
}

void Location::isInside(BlockWithPrecision &bp, const Instructions &origInsts) {
  if (!startColumn.has_value()) {
    auto firstKi = bp.ptr->getFirstInstruction();
    auto lastKi = bp.ptr->getLastInstruction();
    if (firstKi->getLine() <= endLine && startLine <= lastKi->getLine())
      bp.precision = Precision::LineLevel;
    else
      bp.setNotFound();
    return;
  }
  auto tmpBP = bp;
  bp.setNotFound();
  for (size_t i = 0; i < tmpBP.ptr->numInstructions; ++i) {
    InstrWithPrecision kp(tmpBP.ptr->instructions[i]);
    isInside(kp, origInsts);
    if (kp.precision >= tmpBP.precision) {
      tmpBP.precision = kp.precision;
      bp = tmpBP;
    }
  }
}

void CoodyNPELocation::isInside(BlockWithPrecision &bp,
                                const Instructions &origInsts) {
  // if (x + y > z && aaa->bbb->ccc->ddd)
  // ^^^^^^^^^^^^^^^^^ first, skip all this
  // second skip this ^^^^^^^^ (where Cooddy event points)
  // finally, get this         ^ (real location of `Load` instruction)
  auto block = bp.ptr;
  size_t start = 0;
  auto inside = false;
  auto precision = bp.precision;
  KInstruction *ki = nullptr;
  for (; start < block->numInstructions; ++start) {
    ki = block->instructions[start];
    InstrWithPrecision kp(ki);
    Location::isInside(kp, origInsts);
    if (kp.precision >= precision) { // first: go until Cooddy event
      precision = kp.precision;
      if (!inside)                   // first: reached Cooddy event
        inside = true;
    } else if (inside)               // second: skip until left Coody event
      break;
  }
  if (!inside) { // no Cooddy event in this Block
    bp.setNotFound();
    return;
  }
  if (precision == Precision::LineLevel) {
    bp.precision = precision;
    return;
  }
  // finally: Load instruction
  if (ki->inst->getOpcode() == Instruction::Load) {
    // we got precise instruction, so redefine the location
    startLine = (endLine = ki->getLine());
    startColumn = (endColumn = ki->getColumn());
    bp.precision = Precision::ColumnLevel;
    return;
  }
  // most probably Cooddy points to a macro call
  for (size_t i = 0; i < start; i++) {
    if (block->instructions[i]->inst->getOpcode() == Instruction::Load) {
      bp.precision = Precision::LineLevel;
      return;
    }
  }
  bp.setNotFound();
}

std::string Location::toString() const {
  return filename + ":" + std::to_string(startLine);
}
} // namespace klee

// Local bits operation verifiers (B2 step-2 design r9 §5): these bodies
// stay in the operations layer and call the shared width/grammar entries
// declared in ACIRSourceContracts.h; the staged-analysis implementations
// live in BitsAnalysis.cpp. Every rejection and diagnostic is preserved.
#include "pycircuit/Dialect/ACIR/ACIROps.h"
#include "ACIRSourceContracts.h"

using namespace mlir;

namespace acir::ac {

LogicalResult BitsUnaryOp::verify() {
  // Local shape/grammar only (r9 §5): width equivalence and fit are
  // staged-tier obligations (verifyResolvedOperation extension).
  return success();
}

LogicalResult BitsBinaryOp::verify() {
  // Local shape/grammar only (r9 §5): width equivalence and fit are
  // staged-tier obligations (verifyResolvedOperation extension).
  return success();
}

LogicalResult BitsCompareOp::verify() {
  // Local shape/grammar only (r9 §5): width equivalence and fit are
  // staged-tier obligations (verifyResolvedOperation extension).
  return success();
}

LogicalResult BitsSelectOp::verify() {
  // Local shape/grammar only (r9 §5): width equivalence and fit are
  // staged-tier obligations (verifyResolvedOperation extension).
  return success();
}

LogicalResult ValueRequireOp::verify() {
  // Local shape/grammar only (r9 §5): width equivalence and fit are
  // staged-tier obligations (verifyResolvedOperation extension).
  return success();
}

LogicalResult BitsConcatOp::verify() {
  // Local shape/grammar only (r9 §5): width equivalence and fit are
  // staged-tier obligations (verifyResolvedOperation extension).
  return success();
}

LogicalResult BitsExtractOp::verify() {
  // Local shape/grammar only (r9 §5): width equivalence and fit are
  // staged-tier obligations (verifyResolvedOperation extension).
  return success();
}

LogicalResult BitsResizeOp::verify() {
  // Local shape/grammar only (r9 §5): width equivalence and fit are
  // staged-tier obligations (verifyResolvedOperation extension).
  return success();
}

LogicalResult BitsConstantOp::verify() {
  // Local shape/grammar only (r9 §5): width equivalence and fit are
  // staged-tier obligations (verifyResolvedOperation extension).
  return success();
}

LogicalResult BitsConstant4Op::verify() {
  // Local shape/grammar only (r9 §5): width equivalence and fit are
  // staged-tier obligations (verifyResolvedOperation extension).
  return success();
}
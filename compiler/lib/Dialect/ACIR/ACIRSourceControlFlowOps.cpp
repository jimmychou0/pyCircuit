#include "ACIRSourceContracts.h"
#include "pycircuit/Dialect/ACIR/ACIROps.h"

using namespace mlir;

namespace acir::ac {
LogicalResult SourceIndexOp::verify() {
  if (failed(detail::verifyOccurrence(getOccurrence(),
                                      [&] { return emitOpError(); })) ||
      failed(detail::verifySourceSpan(getSourceLocation(),
                                      [&] { return emitOpError(); })))
    return failure();
  return success();
}

LogicalResult SourcePredicateOp::verify() {
  // Width resolution runs staged (r9 §5); this transient transport's
  // structure is checked by its consumers.
  return success();
}
} // namespace acir::ac

#ifndef PYCIRCUIT_DIALECT_ACIR_ROUTEDSTAGEADMISSION_H
#define PYCIRCUIT_DIALECT_ACIR_ROUTEDSTAGEADMISSION_H

#include "mlir/IR/DialectInterface.h"
#include "mlir/IR/Operation.h"

namespace acir {

// Stage-specific routed-package operation admission owned by the dialect
// that declares the operations (B1-boundary.md). The common package
// envelope keeps its own non-stage checks and its concrete common-operation
// allowlist; a dialect extension registers here to admit exactly its routed
// stage classes. Implementing this interface never authorizes publication,
// refinement or execution by itself.
struct RoutedStageAdmission
    : public mlir::DialectInterface::Base<RoutedStageAdmission> {
  // Return true when the operation is one of this dialect's admitted routed
  // stage classes. The default admits nothing.
  virtual bool isRoutedPackageOperation(mlir::Operation *operation) const {
    return false;
  }
};

} // namespace acir

#endif // PYCIRCUIT_DIALECT_ACIR_ROUTEDSTAGEADMISSION_H

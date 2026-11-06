#ifndef PYCIRCUIT_DIALECT_ACIR_TRANSACTIONALCONTRACTS_H
#define PYCIRCUIT_DIALECT_ACIR_TRANSACTIONALCONTRACTS_H
#include "pycircuit/Dialect/ACIR/ACIROps.h"
#include "mlir/IR/BuiltinOps.h"
namespace acir::ac {
// Presence selects this contract; malformed markers never select old semantics.
bool isTransactionalPackage(mlir::Operation *operation);
bool isLinkedTransactionalPackage(mlir::Operation *operation);
bool transactionalTypesEqual(mlir::Type lhs, mlir::Type rhs);
// V2 verifies resource/instance structure only. It is not execution admission.
mlir::LogicalResult verifyTransactionalPackage(mlir::ModuleOp package);
// Staged width-closure entry over the same package walk (B2 step-2 design r3
// row): the enum/struct/table and symbolic-Bits payload widths that require
// HardwareAnalysis live here, declared reachable from the compiler layer for
// the unlinked→linked transition acceptance. The analysis-side acceptance
// (the hardware source-check preflight plan build) invokes it for packages
// carrying ac.transactional; the pipeline transition flow will call the same
// entry when it exists.
mlir::LogicalResult verifyTransactionalLinkedPackage(mlir::ModuleOp package);
mlir::LogicalResult verifyTransactionalOperation(mlir::Operation *operation);
mlir::LogicalResult verifyTransactionalRegions(mlir::Operation *operation);
} // namespace acir::ac
#endif

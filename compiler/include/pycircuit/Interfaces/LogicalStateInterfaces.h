#ifndef PYCIRCUIT_INTERFACES_LOGICALSTATEINTERFACES_H
#define PYCIRCUIT_INTERFACES_LOGICALSTATEINTERFACES_H

#include "mlir/IR/OpDefinition.h"

namespace acir {
#include "pycircuit/Interfaces/LogicalStateInterfaces.h.inc"
} // namespace acir

namespace mlir {
class Operation;
} // namespace mlir

namespace acir {

/// Verify the fixed structural contract of a LogicalStateOpInterface
/// operation: three operands, one result, no regions or successors, and
/// getters aliasing exactly those slots and the declared name/occurrence
/// attributes. Call before consuming any getter of untrusted IR.
mlir::LogicalResult verifyLogicalStateStructure(mlir::Operation *operation);

/// Verify the fixed structural contract of a LogicalStateUpdateOpInterface
/// operation: three operands, no results, regions or successors, and
/// getters aliasing exactly those operand slots.
mlir::LogicalResult
verifyLogicalStateUpdateStructure(mlir::Operation *operation);

} // namespace acir

#endif // PYCIRCUIT_INTERFACES_LOGICALSTATEINTERFACES_H

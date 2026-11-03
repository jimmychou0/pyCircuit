#ifndef PYCIRCUIT_LIB_IR_STATEDECLARATIONCONTRACTS_H
#define PYCIRCUIT_LIB_IR_STATEDECLARATIONCONTRACTS_H

#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/Operation.h"

namespace acir {

// Data-only logical-state declaration contracts shared by the source
// ac.state declaration and the routed gfir.state replacement
// (B1-boundary.md). Every check here inspects operations, values and
// attributes only; none constructs an analysis, evaluates bindings or
// admits a package for publication, refinement or execution.

// Named direct module placement with a valid instance identifier and a
// well-formed occurrence whose origin names the owning module.
mlir::LogicalResult verifyNamedLogicalDeclaration(mlir::Operation *declaration,
                                                  mlir::StringAttr instanceName);

// Direct ac.module graph placement inside a routed package carrying the
// current routed contract marker. This is the structural placement header
// of routed logical-state operations, not stage admission.
mlir::LogicalResult
verifyRoutedLogicalStatePlacement(mlir::Operation *operation);

// Bits control carrier whose width, when a literal integer is directly
// available without binding evaluation, is exactly one. Symbolic widths
// defer to the owning stage's bound check.
mlir::LogicalResult verifyLiteralControlWidth(mlir::Operation *site,
                                              mlir::Value control,
                                              llvm::StringRef description);

// Structural dictionary shape of an ac.payload_constraint attribute: closed
// field set, hardware kind, payload type agreement and known source kind.
// Semantic width, scope and symbolic-template obligations stay with the
// bound stage verifier.
mlir::LogicalResult verifyPayloadConstraintShape(mlir::Operation *site,
                                                 mlir::Attribute raw,
                                                 mlir::Type payload);

} // namespace acir

#endif // PYCIRCUIT_LIB_IR_STATEDECLARATIONCONTRACTS_H

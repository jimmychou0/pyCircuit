#ifndef PYCIRCUIT_LIB_IR_STATEDECLARATIONCONTRACTS_H
#define PYCIRCUIT_LIB_IR_STATEDECLARATIONCONTRACTS_H

#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/Operation.h"

namespace acir::ac::detail {
// Strip origin/location provenance recursively; the single semantic
// equivalence implementation behind areEquivalentHardwareTypes.
mlir::Attribute semanticAttr(mlir::Attribute attribute);
} // namespace acir::ac::detail

namespace acir::ac {
/// Provenance-aware hardware payload equivalence (declaration moved here from
/// HardwareAnalysis.h so common-IR and GFIR checks can use it without the
/// staged analysis layer; HardwareAnalysis.h keeps a matching declaration).
bool areEquivalentHardwareTypes(mlir::Type lhs, mlir::Type rhs);
} // namespace acir::ac

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

namespace acir::ac {

// Attribute-based declaration queries (B2 step-2 design r9): nominal
// struct/enum/table facts read from declaration-operation ATTRIBUTES via
// symbol lookup, without concrete op classes or staged analysis. These
// keep native verifiers' rejections for definite facts; binding-dependent
// facts remain the staged tiers' obligation.
/// Every declared field of the package-level struct named by `symbol`,
/// or failure when the declaration is absent. Fields are the declaration
/// attribute rows verbatim.
mlir::FailureOr<llvm::ArrayRef<mlir::Attribute>>
structFieldList(mlir::Operation *scope, mlir::SymbolRefAttr symbol);

/// The declared member code list of the enum named by `symbol`, or
/// failure when absent.
mlir::FailureOr<mlir::ArrayAttr>
enumMemberList(mlir::Operation *scope, mlir::SymbolRefAttr symbol);

/// Closed payload width of a hardware payload type from declaration
/// attributes and the shared static evaluation: bits/enum/struct/table
/// only, positive and finite, no formal references (callers defer
/// symbolic widths to the staged tiers).
mlir::FailureOr<uint64_t> closedPayloadWidth(mlir::Type payload,
                                             mlir::Operation *site);

/// A literal-Bits width resolves to a definite integer (any value, unlike
/// verifyLiteralControlWidth's width-one rule); non-literal widths
/// succeed vacuously so symbolic carriers defer to the staged tiers.
mlir::LogicalResult verifyLiteralWidthResolves(mlir::Operation *site,
                                               mlir::Value carrier,
                                               llvm::StringRef description);

} // namespace acir::ac

} // namespace acir

#endif // PYCIRCUIT_LIB_IR_STATEDECLARATIONCONTRACTS_H

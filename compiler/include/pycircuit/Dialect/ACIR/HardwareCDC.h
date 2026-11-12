#ifndef PYCIRCUIT_DIALECT_ACIR_HARDWARECDC_H
#define PYCIRCUIT_DIALECT_ACIR_HARDWARECDC_H
#include "pycircuit/Dialect/ACIR/HardwareAnalysis.h"
namespace acir::ac {
mlir::LogicalResult verifyBoundHardwareCDCOperation(
    const HardwareAnalysis &analysis, mlir::Operation *operation,
    const HardwareBindings &bindings);
mlir::LogicalResult verifyCDCRefinementContract(mlir::Operation *witness);
/// Authenticate FIFO endpoint authority independently of its component graph.
/// This proof does not invoke component verification.
mlir::LogicalResult verifyCDCRefinementIntents(
    const HardwareAnalysis &analysis, CDCRefinementOp witness,
    const HardwareBindings &bindings);
mlir::FailureOr<llvm::DenseMap<mlir::Operation *, mlir::Operation *>>
getVerifiedCDCComponentOwners(mlir::ModuleOp package);
mlir::LogicalResult verifyHardwareCDCComponent(mlir::Operation *witness);
mlir::LogicalResult verifyFinalHardwareCDCComponents(mlir::ModuleOp package);
struct HardwareCDCEndpointEffect {
  mlir::Operation *resource;
  mlir::Value clock, reset, intent, availability;
  RegisterEdge edge;
  RegisterResetPolarity polarity;
  bool write;
};
mlir::FailureOr<llvm::SmallVector<HardwareCDCEndpointEffect>>
getAsyncFIFOEndpointEffects(RuleOp rule);
bool hasLogicalCDC(mlir::Operation *root);
mlir::FailureOr<mlir::OwningOpRef<mlir::ModuleOp>>
materializeHardwareCDC(mlir::ModuleOp logical,
                       HardwareSourceCheckLimits limits = {});
mlir::LogicalResult
verifyHardwareCDCMaterialization(mlir::ModuleOp logical,
                                 mlir::ModuleOp expanded,
                                 HardwareSourceCheckLimits limits = {});
} // namespace acir::ac
#endif

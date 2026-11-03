#include "pycircuit/Dialect/ACIR/LogicalStateValidation.h"

#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Operation.h"
#include "SourceCheckBudget.h"
#include "pycircuit/Dialect/ACIR/ACIROps.h"
#include "pycircuit/Dialect/ACIR/HardwareAnalysis.h"

using namespace mlir;

namespace acir::ac {

HardwareBindings stateOwnerScope(Operation *operation) {
  HardwareBindings bindings;
  bindings.owner = operation->getParentOfType<ModuleOp>();
  return bindings;
}

LogicalResult stateControlPredicate(const HardwareAnalysis &analysis,
                                    Value value,
                                    const HardwareBindings &bindings,
                                    Operation *site, llvm::StringRef description) {
  if (!isa<BitsType>(value.getType()))
    return site->emitOpError() << description << " requires width-one Bits";
  auto width = analysis.getPackedWidth(value.getType(), bindings, site);
  if (failed(width))
    return failure();
  if (*width != 1)
    return site->emitOpError() << description << " requires width-one Bits";
  return success();
}

FailureOr<acir::LogicalStateUpdateOpInterface>
admissionPairLogicalStateUpdate(acir::LogicalStateOpInterface state,
                                detail::SourceCheckBudget &budget) {
  if (failed(acir::verifyLogicalStateStructure(state.getOperation())))
    return failure();
  acir::LogicalStateUpdateOpInterface result;
  for (auto *user : state.getStateQ().getUsers()) {
    // Same single-unit admission charge as the previous admission helper.
    if (failed(budget.debit(1, state.getOperation())))
      return failure();
    auto update = dyn_cast<acir::LogicalStateUpdateOpInterface>(user);
    if (!update || update.getUpdatedStateQ() != state.getStateQ())
      continue;
    // One operation may use Q in both the state and candidate positions.
    if (result && result != update)
      return state.getOperation()->emitOpError()
             << "logical state has multiple update bindings";
    result = update;
  }
  if (!result)
    return state.getOperation()->emitOpError()
           << "logical state has no update binding";
  return result;
}

LogicalResult verifyLogicalStateUpdateBinding(
    acir::LogicalStateUpdateOpInterface update, const HardwareAnalysis *shared,
    const HardwareBindings *sharedBindings) {
  Operation *operation = update.getOperation();
  if (failed(acir::verifyLogicalStateUpdateStructure(operation)))
    return failure();
  auto owner = dyn_cast_or_null<ModuleOp>(operation->getParentOp());
  auto package =
      owner ? dyn_cast_or_null<mlir::ModuleOp>(owner->getParentOp())
            : mlir::ModuleOp();
  auto stage = package ? package->getAttrOfType<StringAttr>("ac.stage")
                       : StringAttr();
  auto contract = package
                      ? package->getAttrOfType<IntegerAttr>("ac.routed_contract")
                      : IntegerAttr();
  if (!owner || !package || !stage || stage.getValue() != "routed" ||
      package->hasAttr("ac.unit_kind") || !contract ||
      !contract.getType().isSignlessInteger(64) ||
      (contract.getValue() != 2 && contract.getValue() != 3))
    return operation->emitOpError()
           << "requires direct module graph placement in a versioned routed "
              "package";
  if (!operation->getAttrs().empty())
    return operation->emitOpError()
           << "state update does not accept attributes";
  // The defining routed declaration stays concrete until the GFIR state
  // migration replaces the routed state operation class.
  auto state =
      update.getUpdatedStateQ().getDefiningOp<SourceStateOp>();
  if (!state || state->getBlock() != operation->getBlock())
    return operation->emitOpError()
           << "state must be the direct result of a same-module ac.state";
  if (auto stateContract = dyn_cast<acir::LogicalStateOpInterface>(*state);
      stateContract &&
      failed(acir::verifyLogicalStateStructure(stateContract.getOperation())))
    return failure();
  if (!areEquivalentHardwareTypes(update.getUpdatedStateQ().getType(),
                                  update.getStateCandidate().getType()))
    return operation->emitOpError()
           << "candidate must exactly match the state payload type";
  HardwareAnalysis owned(package);
  const HardwareAnalysis &analysis = shared ? *shared : owned;
  HardwareBindings ownedBindings = stateOwnerScope(operation);
  const HardwareBindings &bindings =
      sharedBindings ? *sharedBindings : ownedBindings;
  return stateControlPredicate(analysis, update.getStateWriteIntent(),
                               bindings, operation, "state update intent");
}

} // namespace acir::ac

// Staged width-closure entry for transactional resource packages (B2 step-2
// design r3 row). The enum/struct/table and symbolic-Bits payload width
// checks moved verbatim out of the native verifyTransactionalPackage walk:
// they require HardwareAnalysis (resolveEnum/lookupStruct/getTableSize move
// analysis-side), so the native hook keeps only the TypeID-free grammar and
// the literal-Bits carrier width (verifyLiteralWidthResolves). This TU joins
// ACIRAnalysis at IX; the compiler-layer transition hook calls the entry
// declared in TransactionalContracts.h.
#include "pycircuit/Dialect/ACIR/TransactionalContracts.h"
#include "ACIRSourceContracts.h"
#include "pycircuit/Dialect/ACIR/HardwareAnalysis.h"
#include <functional>

using namespace mlir;
namespace acir::ac {
namespace {
Type payload(Type type) {
  if (auto t = dyn_cast<PrimitiveType>(type)) return t.getPayload();
  if (auto t = dyn_cast<StateType>(type)) return t.getPayload();
  if (auto t = dyn_cast<QueueType>(type)) return t.getPayload();
  return type;
}
FunctionType signature(Operation *op) {
  auto attr = op ? op->getAttrOfType<TypeAttr>("function_type") : TypeAttr();
  return attr ? dyn_cast<FunctionType>(attr.getValue()) : FunctionType();
}
} // namespace

LogicalResult verifyTransactionalLinkedPackage(mlir::ModuleOp package) {
  HardwareAnalysis analysis(package);
  // The moved width-closure half of the native checkType lambda, verbatim:
  // scope and packed-width rejections keep their exact sites and texts. The
  // literal-Bits carrier width subset stays with the native hook, so a
  // literal width that passed there re-resolves here without a duplicate
  // diagnostic; the concrete-payload and module-reference guards re-run so a
  // direct staged call stays safe on mutated IR.
  std::function<LogicalResult(Type, Operation *)> checkType = [&](Type type,
                                                                  Operation *site) -> LogicalResult {
    if (isa<ModuleRefType>(type)) return success();
    type = payload(type);
    if (!isa<BitsType, EnumType, StructType, TableType>(type))
      return site->emitOpError("transactional resource contract requires concrete hardware payloads");
    HardwareBindings bindings;
    bindings.owner = isa<ModuleOp, ModuleImportOp>(site) ? site : site->getParentOfType<ModuleOp>().getOperation();
    if (bindings.owner && failed(detail::verifyHardwareTypeScope(type, bindings.owner))) return failure();
    return success(succeeded(analysis.getPackedWidth(type, bindings, site)));
  };
  auto fail = package.walk([&](Operation *op) {
    if (op == package.getOperation()) return WalkResult::advance();
    for (Type type : op->getOperandTypes()) if (failed(checkType(type, op))) return WalkResult::interrupt();
    for (Type type : op->getResultTypes()) if (failed(checkType(type, op))) return WalkResult::interrupt();
    if (auto sig = signature(op)) {
      for (Type type : sig.getInputs()) if (failed(checkType(type, op))) return WalkResult::interrupt();
      for (Type type : sig.getResults()) if (failed(checkType(type, op))) return WalkResult::interrupt();
    }
    return WalkResult::advance();
  });
  return failure(fail.wasInterrupted());
}
} // namespace acir::ac

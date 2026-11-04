#include "pycircuit/Dialect/ACIR/HardwareAnalysis.h"
#include "ACIRSourceContracts.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringSet.h"

using namespace mlir;

namespace acir::ac {
namespace {


HardwareBindings scope(Operation *operation) {
  HardwareBindings bindings;
  bindings.owner = operation->getParentOfType<ModuleOp>();
  return bindings;
}
} // namespace

LogicalResult EnumOp::verify() {
  return success(succeeded(validateEnum(*this, *this)));
}

LogicalResult EnumCreateOp::verify() {
  return detail::verifyEnumValueLocally(*this);
}
LogicalResult EnumToBitsOp::verify() {
  return detail::verifyEnumValueLocally(*this);
}
LogicalResult EnumFromBitsOp::verify() {
  return detail::verifyEnumValueLocally(*this);
}

} // namespace acir::ac

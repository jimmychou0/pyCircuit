#include "ACIRSourceContracts.h"
#include "pycircuit/Dialect/ACIR/ACIROps.h"
#include "pycircuit/Dialect/ACIR/HardwareAnalysis.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringSet.h"

using namespace mlir;

namespace acir::ac {

LogicalResult EnumCreateOp::verify() {
  return detail::verifyEnumValueLocally(*this);
}
LogicalResult EnumToBitsOp::verify() {
  return detail::verifyEnumValueLocally(*this);
}
LogicalResult EnumFromBitsOp::verify() {
  return detail::verifyEnumValueLocally(*this);
}


LogicalResult EnumOp::verify() {
  return success(succeeded(validateEnumDeclaration(*this, *this)));
}
} // namespace acir::ac

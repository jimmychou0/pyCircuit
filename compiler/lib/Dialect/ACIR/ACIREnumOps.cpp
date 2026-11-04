#include "ACIRSourceContracts.h"
#include "pycircuit/Dialect/ACIR/ACIROps.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringSet.h"

using namespace mlir;

namespace acir::ac {
namespace {


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

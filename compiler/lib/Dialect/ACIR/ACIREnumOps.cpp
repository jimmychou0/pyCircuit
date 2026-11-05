#include "ACIRSourceContracts.h"
#include "pycircuit/Dialect/ACIR/ACIROps.h"
#include "pycircuit/Dialect/ACIR/HardwareAnalysis.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringSet.h"

using namespace mlir;

namespace acir::ac {

// Native local verifiers (r9 §5 reroute): grammar only. The enum-value
// semantic checks run at the staged entries (HardwareAnalysis::verify via
// --ac-verify-hardware for graph fixtures; the source-tier walk for
// source units; the routing preparation for transaction bodies), with the
// lit fixtures pinning the diagnostics at the new site.
LogicalResult EnumCreateOp::verify() { return success(); }
LogicalResult EnumToBitsOp::verify() { return success(); }
LogicalResult EnumFromBitsOp::verify() { return success(); }


LogicalResult EnumOp::verify() {
  return success(succeeded(validateEnumDeclaration(*this, *this)));
}
} // namespace acir::ac

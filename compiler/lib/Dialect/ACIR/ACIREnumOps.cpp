#include "ACIRSourceContracts.h"
#include "pycircuit/Dialect/ACIR/ACIROps.h"
#include "pycircuit/Dialect/ACIR/HardwareAnalysis.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringSet.h"

using namespace mlir;

namespace acir::ac {

// Native local verifiers (r9 §5 reroute): structure/grammar only. The
// enum-value semantic checks run at the staged entries — the source-tier
// walk in SourceUnitValidation (requireResolved=false) and
// verifyResolvedOperation for routed/final packages — with their
// diagnostics unchanged at the new sites.
LogicalResult EnumCreateOp::verify() { return success(); }
LogicalResult EnumToBitsOp::verify() { return success(); }
LogicalResult EnumFromBitsOp::verify() { return success(); }


LogicalResult EnumOp::verify() {
  return success(succeeded(validateEnumDeclaration(*this, *this)));
}
} // namespace acir::ac

// Generated attribute/type definitions of the ac dialect (B2 step-2 design
// r9): per-def storage, getters, TypeID anchors and the default
// parse/print hooks. Compiled in the core layer so ACIRCore's archive
// carries these symbols below the operations layer; the dialect class and
// op registration stay in ACIRRegistration.cpp.
#include "pycircuit/Dialect/ACIR/ACIRDialect.h"

#include "mlir/IR/DialectImplementation.h"
#include "llvm/ADT/TypeSwitch.h"

#include "pycircuit/Dialect/ACIR/ACIREnums.cpp.inc"

#define GET_ATTRDEF_CLASSES
#include "pycircuit/Dialect/ACIR/ACIRAttributes.cpp.inc"

#define GET_TYPEDEF_CLASSES
#include "pycircuit/Dialect/ACIR/ACIRTypes.cpp.inc"

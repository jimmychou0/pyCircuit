#include "pycircuit/Dialect/GFIR/GFIRDialect.h"
#include "pycircuit/Dialect/ACIR/RoutedStageAdmission.h"
#include "pycircuit/Dialect/GFIR/GFIROps.h"

#include "pycircuit/Dialect/GFIR/GFIRDialect.cpp.inc"

using namespace mlir;

namespace acir::gfir {

// The routed stage admits exactly this dialect's two logical-state classes
// (B1-boundary.md). Interface presence or dialect registration alone never
// admits any other operation, and no user extension op is accepted.
struct GFIRRoutedStageAdmission : acir::RoutedStageAdmission {
  using acir::RoutedStageAdmission::Base;
  bool isRoutedPackageOperation(Operation *operation) const override {
    return isa<StateOp, StateUpdateOp>(operation);
  }
};

void GFIRDialect::initialize() {
  addInterfaces<GFIRRoutedStageAdmission>();
  addOperations<
#define GET_OP_LIST
#include "pycircuit/Dialect/GFIR/GFIROps.cpp.inc"
      >();
}

} // namespace acir::gfir

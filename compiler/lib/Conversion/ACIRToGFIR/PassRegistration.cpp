#include "pycircuit/Conversion/ACIRToGFIR.h"

#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassRegistry.h"

namespace acir {
void registerACIRToGFIRPasses() {
  registerPass([] { return createRouteTransactionsPass(); });
}
} // namespace acir

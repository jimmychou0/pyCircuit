#include "pycircuit/Conversion/GFIRToHWIR.h"

#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassRegistry.h"

namespace acir {
void registerGFIRToHWIRPasses() {
  registerPass([] { return createLowerTransactionsPass(); });
}
} // namespace acir

#include "pycircuit/Transforms/Passes.h"

#include "mlir/Pass/PassRegistry.h"

namespace acir {
void registerACIRPasses() {
  registerPass([] { return createRouteTransactionsPass(); });
  registerPass([] { return createNormalizeSourceControlFlowPass(); });
  registerPass([] { return createExtractSourceInterfacePass(); });
  registerPass([] { return createSimplifyRecordWiresPass(); });
  registerPass([] { return createAnalyzeSourceMethodSummariesPass(); });
  registerPass([] { return createAnalyzeTransactionStateSSAPass(); });
  registerPass([] { return createLowerTransactionsPass(); });
}
} // namespace acir

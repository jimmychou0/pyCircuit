#ifndef PYCIRCUIT_DIALECT_ACIR_TRANSACTIONALCLOSUREHOST_H
#define PYCIRCUIT_DIALECT_ACIR_TRANSACTIONALCLOSUREHOST_H

#include "mlir/IR/DialectInterface.h"
#include "mlir/IR/BuiltinOps.h"

namespace acir::ac {

// Host for the transactional payload-closure acceptance (r8 review): the
// closure is analysis-owned, so the native transactional verifier reaches
// it through this dialect interface instead of linking the analysis layer.
// The interface identity is explicitly emitted in this header's home target
// (shared interfaces), mirroring GrammarCacheHost. The native verifier is
// fail-closed: without a registered host the closure cannot run and the
// package is rejected.
class TransactionalClosureHost
    : public mlir::DialectInterface::Base<TransactionalClosureHost> {
public:
  using Base::Base;
  virtual ~TransactionalClosureHost() = default;
  // Run the staged payload-closure walk (enum/struct/table widths and
  // concrete-payload agreement) over the transactional package.
  virtual mlir::LogicalResult verifyLinkedWidths(mlir::ModuleOp package) const = 0;
};


} // namespace acir::ac

MLIR_DECLARE_EXPLICIT_TYPE_ID(::acir::ac::TransactionalClosureHost)

#endif // PYCIRCUIT_DIALECT_ACIR_TRANSACTIONALCLOSUREHOST_H

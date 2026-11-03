#ifndef PYCIRCUIT_DIALECT_ACIR_HARDWAREANALYSISCONTEXT_H
#define PYCIRCUIT_DIALECT_ACIR_HARDWAREANALYSISCONTEXT_H

#include "mlir/IR/Attributes.h"
#include "mlir/IR/Types.h"
#include "llvm/ADT/StringMap.h"
#include <cstdint>

namespace mlir {
class Operation;
}
namespace acir::ac {
/// Bindings describe an occurrence of an existing definition, not cloned IR.
struct HardwareBindings {
  mlir::Operation *owner = nullptr;
  llvm::StringMap<mlir::Attribute> integers;
  llvm::StringMap<mlir::Type> types;
};

struct HardwareSourceCheckLimits {
  uint64_t maxWorkUnits = 4 * 1024 * 1024;
};
} // namespace acir::ac
#endif

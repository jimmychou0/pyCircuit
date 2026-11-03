#ifndef PYCIRCUIT_PACKAGE_VERIFICATION_H
#define PYCIRCUIT_PACKAGE_VERIFICATION_H

#include "mlir/Support/LogicalResult.h"
#include <cstdint>
#include <functional>

namespace mlir {
class Operation;
}
namespace acir::ac {
class HardwareAnalysis;
}

namespace acir::ac::detail {
/// Stage policy for the shared bounded package-check pipeline. The profile
/// changes admission, not the source-check builder's ordering or lifetime.
enum class PackageVerificationProfile { Routed, Hardware };

mlir::LogicalResult
verifyHardwarePackageStage(const HardwareAnalysis &analysis,
                           PackageVerificationProfile profile);
mlir::LogicalResult
verifyHardwarePackageEnvelope(
    const HardwareAnalysis &analysis, PackageVerificationProfile profile,
    const std::function<mlir::LogicalResult(uint64_t, mlir::Operation *)> &debit);
} // namespace acir::ac::detail
#endif

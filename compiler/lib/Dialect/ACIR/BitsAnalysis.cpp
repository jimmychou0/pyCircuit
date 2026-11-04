// Width-dependent bits operation verification (B2 step-2 design r9):
// equivalentWidths constructs the staged analysis, so these verify bodies
// live analysis-side; the analysis-free bits verifiers (constants and the
// type verifier, relocated to the core TU) stay below.
#include "pycircuit/Dialect/ACIR/HardwareAnalysis.h"
#include "pycircuit/Dialect/ACIR/ACIROps.h"

using namespace mlir;

namespace acir::ac {
namespace {

FailureOr<bool> equivalentWidths(Operation *site, BitsType lhs, BitsType rhs) {
  if (areEquivalentHardwareTypes(lhs, rhs)) return true;
  detail::SourceCheckBudget budget(HardwareSourceCheckLimits{});
  budget.setPhase("closed Bits width comparison");
  detail::SourceCheckBudget::RawDiagnosticScope rawDiagnostics(budget);
  if (failed(prepayClosedWidth(Type(lhs), site, budget)) ||
      failed(prepayClosedWidth(Type(rhs), site, budget)))
    return failure();
  // No package scan, defaults or instance facts belong in this fallback.
  // Unresolved/nominal-dependent widths retain the symbolic comparison above.
  HardwareAnalysis hardware(mlir::ModuleOp{});
  const auto checkingThread = std::this_thread::get_id();
  ScopedDiagnosticHandler quiet(site->getContext(), [&](Diagnostic &) {
    return success(std::this_thread::get_id() == checkingThread);
  });
  auto left = hardware.evaluateStatic(lhs.getWidth(), {}, site);
  if (failed(left)) return false;
  auto right = hardware.evaluateStatic(rhs.getWidth(), {}, site);
  if (failed(right)) return false;
  auto leftInteger = dyn_cast<MathIntAttr>(*left);
  auto rightInteger = dyn_cast<MathIntAttr>(*right);
  if (!leftInteger || !rightInteger) return false;
  llvm::APSInt leftWidth(leftInteger.getCanonicalValue());
  llvm::APSInt rightWidth(rightInteger.getCanonicalValue());
  return isPositive(leftWidth) && isPositive(rightWidth) &&
         leftWidth.getActiveBits() <= 64 && rightWidth.getActiveBits() <= 64 &&
         leftWidth.getZExtValue() == rightWidth.getZExtValue();
}

LogicalResult BitsUnaryOp::verify() {
  if (getOpcode() != "not")
    return emitOpError() << "opcode must be 'not'";
  auto sameWidth = equivalentWidths(*this, getInput().getType(), getResult().getType());
  if (failed(sameWidth)) return failure();
  if (!*sameWidth)
    return emitOpError()
           << "unary operand and result must have the same symbolic width";
  return success();
}

LogicalResult BitsBinaryOp::verify() {
  if (!isClosed(getOpcode(), {"add", "sub", "mul", "udiv", "urem", "and", "or",
                              "xor", "shl", "lshr", "ashr"}))
    return emitOpError() << "unknown closed bit-vector binary opcode";
  return requireSameWidth(*this, getLhs().getType(), getRhs().getType(),
                          getResult().getType());
}

LogicalResult BitsCompareOp::verify() {
  if (!isClosed(getPredicate(), {"eq", "ne", "case_eq", "ult", "ule", "ugt",
                                 "uge", "slt", "sle", "sgt", "sge"}))
    return emitOpError() << "unknown closed bit-vector comparison predicate";
  auto sameWidth = equivalentWidths(*this, getLhs().getType(), getRhs().getType());
  if (failed(sameWidth)) return failure();
  if (!*sameWidth)
    return emitOpError() << "comparison operands must have the same width";
  return requireOneBit(getResult().getType(), *this, "comparison result");
}

LogicalResult BitsSelectOp::verify() {
  if (failed(
          requireOneBit(getCondition().getType(), *this, "select condition")))
    return failure();
  return requireSameWidth(*this, getTrueValue().getType(),
                          getFalseValue().getType(), getResult().getType());
}

LogicalResult ValueRequireOp::verify() {
  auto error = [&] { return emitOpError(); };
  auto package = (*this)->getParentOfType<mlir::ModuleOp>();
  if (auto stage = package ? package->getAttrOfType<StringAttr>("ac.stage") : StringAttr();
      stage && stage.getValue() == "source")
    return emitOpError() << "is compiler-owned final availability evidence";
  if (!isa<RuleOp, ModuleOp>((*this)->getParentOp()))
    return emitOpError() << "requires an immediate hardware Module or Rule";
  if (failed(requireOneBit(getAvailability().getType(), *this,
                           "required availability")) ||
      failed(requireOneBit(getPath().getType(), *this, "required use path")) ||
      failed(requireOneBit(getResult().getType(), *this, "required use result")) ||
      failed(detail::verifyOccurrence(getOccurrence(), error)) ||
      failed(detail::verifySourceSpan(getSourceLocation(), error)))
    return failure();
  return success();
}

LogicalResult BitsConcatOp::verify() {
  if (getInputs().empty())
    return emitOpError() << "concat requires at least one input";
  uint64_t total = 0;
  bool allLiteral = true;
  for (Value input : getInputs()) {
    auto width = literalInteger(cast<BitsType>(input.getType()).getWidth());
    if (!width || width->isNegative() || width->getActiveBits() > 64 ||
        total > std::numeric_limits<uint64_t>::max() - width->getZExtValue()) {
      allLiteral = false;
      break;
    }
    total += width->getZExtValue();
  }
  auto resultWidth = literalInteger(getResult().getType().getWidth());
  if (allLiteral && resultWidth &&
      (resultWidth->isNegative() || resultWidth->getActiveBits() > 64 ||
       resultWidth->getZExtValue() != total))
    return emitOpError() << "concat result width must equal input width sum";
  return success();
}

LogicalResult BitsExtractOp::verify() {
  if (failed(StaticExprAttr::verify([&] { return emitOpError(); },
                                    getLow().getTree())))
    return failure();
  auto low = literalInteger(getLow());
  auto inputWidth = literalInteger(getInput().getType().getWidth());
  auto resultWidth = literalInteger(getResult().getType().getWidth());
  if (low && low->isNegative())
    return emitOpError() << "extract low bound must be nonnegative";
  if (low && inputWidth && resultWidth && low->getActiveBits() <= 64 &&
      inputWidth->getActiveBits() <= 64 && resultWidth->getActiveBits() <= 64 &&
      (resultWidth->isNegative() ||
       low->getZExtValue() > inputWidth->getZExtValue() ||
       resultWidth->getZExtValue() >
           inputWidth->getZExtValue() - low->getZExtValue()))
    return emitOpError() << "extract slice exceeds its input width";
  return success();
}

LogicalResult BitsResizeOp::verify() {
  if (!isClosed(getMode(), {"trunc", "zext", "sext"}))
    return emitOpError() << "resize mode must be trunc, zext, or sext";
  auto inputWidth = literalInteger(getInput().getType().getWidth());
  auto resultWidth = literalInteger(getResult().getType().getWidth());
  if (!inputWidth || !resultWidth)
    return success();
  if ((getMode() == "trunc" && *resultWidth >= *inputWidth) ||
      (getMode() != "trunc" && *resultWidth <= *inputWidth))
    return emitOpError() << "trunc must narrow and extension modes must widen";
  return success();
}

} // namespace
} // namespace acir::ac

// Local bits operation verifiers (B2 step-2 design r9 §5): these bodies
// stay in the operations layer and call the shared width/grammar entries
// declared in ACIRSourceContracts.h; the staged-analysis implementations
// live in BitsAnalysis.cpp. Every rejection and diagnostic is preserved.
#include "pycircuit/Dialect/ACIR/ACIROps.h"
#include "ACIRSourceContracts.h"

using namespace mlir;

namespace acir::ac {

namespace acir::ac {

LogicalResult BitsUnaryOp::verify() {
  if (getOpcode() != "not")
    return emitOpError() << "opcode must be 'not'";
  auto sameWidth = bitsWidthsEquivalent(*this, getInput().getType(), getResult().getType());
  if (failed(sameWidth)) return failure();
  if (!*sameWidth)
    return emitOpError()
           << "unary operand and result must have the same symbolic width";
  return success();
}

LogicalResult BitsBinaryOp::verify() {
  if (!bitsOpcodeClosed(getOpcode(), {"add", "sub", "mul", "udiv", "urem", "and", "or",
                              "xor", "shl", "lshr", "ashr"}))
    return emitOpError() << "unknown closed bit-vector binary opcode";
  return bitsRequireSameWidth(*this, getLhs().getType(), getRhs().getType(),
                          getResult().getType());
}

LogicalResult BitsCompareOp::verify() {
  if (!bitsOpcodeClosed(getPredicate(), {"eq", "ne", "case_eq", "ult", "ule", "ugt",
                                 "uge", "slt", "sle", "sgt", "sge"}))
    return emitOpError() << "unknown closed bit-vector comparison predicate";
  auto sameWidth = bitsWidthsEquivalent(*this, getLhs().getType(), getRhs().getType());
  if (failed(sameWidth)) return failure();
  if (!*sameWidth)
    return emitOpError() << "comparison operands must have the same width";
  return bitsRequireOneBit(getResult().getType(), *this, "comparison result");
}

LogicalResult BitsSelectOp::verify() {
  if (failed(
          bitsRequireOneBit(getCondition().getType(), *this, "select condition")))
    return failure();
  return bitsRequireSameWidth(*this, getTrueValue().getType(),
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
  if (failed(bitsRequireOneBit(getAvailability().getType(), *this,
                           "required availability")) ||
      failed(bitsRequireOneBit(getPath().getType(), *this, "required use path")) ||
      failed(bitsRequireOneBit(getResult().getType(), *this, "required use result")) ||
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
    auto width = bitsLiteralInteger(cast<BitsType>(input.getType()).getWidth());
    if (!width || width->isNegative() || width->getActiveBits() > 64 ||
        total > std::numeric_limits<uint64_t>::max() - width->getZExtValue()) {
      allLiteral = false;
      break;
    }
    total += width->getZExtValue();
  }
  auto resultWidth = bitsLiteralInteger(getResult().getType().getWidth());
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
  auto low = bitsLiteralInteger(getLow());
  auto inputWidth = bitsLiteralInteger(getInput().getType().getWidth());
  auto resultWidth = bitsLiteralInteger(getResult().getType().getWidth());
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
  if (!bitsOpcodeClosed(getMode(), {"trunc", "zext", "sext"}))
    return emitOpError() << "resize mode must be trunc, zext, or sext";
  auto inputWidth = bitsLiteralInteger(getInput().getType().getWidth());
  auto resultWidth = bitsLiteralInteger(getResult().getType().getWidth());
  if (!inputWidth || !resultWidth)
    return success();
  if ((getMode() == "trunc" && *resultWidth >= *inputWidth) ||
      (getMode() != "trunc" && *resultWidth <= *inputWidth))
    return emitOpError() << "trunc must narrow and extension modes must widen";
  return success();
}
} // namespace acir::ac

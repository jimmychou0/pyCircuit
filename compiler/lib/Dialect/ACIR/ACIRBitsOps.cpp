#include "pycircuit/Dialect/ACIR/ACIROps.h"
#include "pycircuit/Dialect/ACIR/HardwareAnalysis.h"
#include "ACIRSourceContracts.h"
#include "SourceCheckBudget.h"

#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/STLExtras.h"

#include <limits>
#include <optional>
#include <thread>

using namespace mlir;

namespace acir::ac {
namespace {

std::optional<llvm::APSInt> literalInteger(StaticExprAttr expression) {
  if (!expression)
    return std::nullopt;
  DictionaryAttr tree = expression.getTree();
  auto kind = tree ? tree.getAs<StringAttr>("kind") : StringAttr();
  auto value = tree ? tree.getAs<DictionaryAttr>("value") : DictionaryAttr();
  auto valueKind = value ? value.getAs<StringAttr>("kind") : StringAttr();
  auto integer = value ? value.getAs<MathIntAttr>("value") : MathIntAttr();
  if (!kind || kind.getValue() != "literal" || !valueKind ||
      valueKind.getValue() != "integer" || !integer)
    return std::nullopt;
  return llvm::APSInt(integer.getCanonicalValue());
}

bool isPositive(const llvm::APSInt &value) {
  return !value.isNegative() && !value.isZero();
}

// Invocation results can have canonical widths while their callers retain a
// closed authored expression. Prepay the graph before using the shared evaluator.

LogicalResult requireSameWidth(Operation *operation, BitsType lhs, BitsType rhs,
                               BitsType result) {
  auto operands = equivalentWidths(operation, lhs, rhs);
  if (failed(operands)) return failure();
  auto output = equivalentWidths(operation, lhs, result);
  if (failed(output)) return failure();
  if (!*operands || !*output)
    return operation->emitOpError()
           << "operands and result must have the same symbolic width";
  return success();
}

bool isClosed(StringRef value, ArrayRef<StringRef> choices) {
  return llvm::is_contained(choices, value);
}

LogicalResult requireOneBit(BitsType type, Operation *operation,
                            StringRef description) {
  auto width = literalInteger(type.getWidth());
  if (!width || *width != 1)
    return operation->emitOpError()
           << description << " requires canonical literal width one";
  return success();
}

} // namespace

LogicalResult BitsConstantOp::verify() {
  if (failed(StaticExprAttr::verify([&] { return emitOpError(); },
                                    getValue().getTree())))
    return failure();
  auto value = literalInteger(getValue());
  auto width = literalInteger(getResult().getType().getWidth());
  if (value && value->isNegative())
    return emitOpError() << "bits constant must be nonnegative";
  if (value && width && width->getActiveBits() <= 64 &&
      value->getActiveBits() > width->getZExtValue())
    return emitOpError() << "bits constant does not fit its result width";
  return success();
}

LogicalResult BitsConstant4Op::verify() {
  StringRef pattern = getPattern();
  if (pattern.empty() || !llvm::all_of(pattern, [](char bit) {
        return bit == '0' || bit == '1' || bit == 'x' || bit == 'z';
      }))
    return emitOpError()
           << "pattern requires nonempty canonical lowercase 0/1/x/z";
  auto width = literalInteger(getResult().getType().getWidth());
  if (!width || width->isNegative() || width->getActiveBits() > 64 ||
      width->getZExtValue() != pattern.size())
    return emitOpError()
           << "pattern length must equal the positive literal result width";
  return success();
}

} // namespace acir::ac

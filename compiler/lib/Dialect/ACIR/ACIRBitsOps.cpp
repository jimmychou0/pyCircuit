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
LogicalResult prepayClosedWidth(
    llvm::PointerUnion<Attribute, Type> node, Operation *site,
    detail::SourceCheckBudget &budget, unsigned depth = 0,
    uint64_t repetitions = 1) {
  using Budget = detail::SourceCheckBudget;
  if (!node) return success();
  if (depth > 128)
    return emitError(UnknownLoc::get(site->getContext()))
           << "closed width comparison depth exceeded";
  uint64_t visits = Budget::multiply(repetitions, 4 * uint64_t(depth + 1));
  auto pay = [&](uint64_t units) {
    return budget.debit(Budget::multiply(visits, units), site);
  };
  if (failed(pay(1))) return failure();
  bool valid = true;
  auto attribute = [&](Attribute value) {
    if (valid && value && failed(prepayClosedWidth(
                             value, site, budget, depth + 1, repetitions)))
      valid = false;
  };
  auto type = [&](Type value) {
    if (valid && value && failed(prepayClosedWidth(
                             value, site, budget, depth + 1, repetitions)))
      valid = false;
  };
  if (auto value = dyn_cast<Attribute>(node)) {
    uint64_t bytes = 0, slots = 0, slotBytes = 0;
    if (auto string = dyn_cast<StringAttr>(value)) bytes = string.getValue().size();
    if (auto integer = dyn_cast<MathIntAttr>(value))
      bytes = integer.getCanonicalValue().size();
    if (auto integer = dyn_cast<IntegerAttr>(value))
      bytes = (uint64_t(integer.getValue().getBitWidth()) + 7) / 8;
    if (auto dictionary = dyn_cast<DictionaryAttr>(value)) {
      slots = dictionary.size();
      slotBytes = sizeof(NamedAttribute);
    }
    if (auto array = dyn_cast<ArrayAttr>(value)) {
      slots = array.size();
      slotBytes = sizeof(Attribute);
    }
    if (failed(pay(Budget::add(bytes, Budget::add(
                     Budget::multiply(slots, slots + 1),
                     Budget::bytes(Budget::multiply(slots + 1, slotBytes)))))))
      return failure();
    if (auto expression = dyn_cast<StaticExprAttr>(value)) {
      auto kind = expression.getTree().getAs<StringAttr>("kind");
      // type_width re-enters packed layout/type resolution. Pay its repeated
      // substitution/probe/evaluation at every nesting, without resetting cost.
      if (kind && kind.getValue() == "type_width")
        repetitions = Budget::multiply(repetitions, 8);
    }
    value.walkImmediateSubElements(attribute, type);
  } else {
    cast<Type>(node).walkImmediateSubElements(attribute, type);
  }
  return success(valid);
}

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

// Width-dependent bits verification entries (B2 step-2 design r9 §5):
// implementations live analysis-side; the operations-layer verify bodies
// call them through ACIRSourceContracts.h detail declarations. Every
// rejection and diagnostic is preserved at the same native call site.
#include "pycircuit/Dialect/ACIR/HardwareAnalysis.h"
#include "ACIRSourceContracts.h"
#include "Dialect/ACIR/SourceCheckBudget.h"
#include "pycircuit/Dialect/ACIR/ACIROps.h"
#include <thread>

using namespace mlir;

namespace acir::ac {
namespace detail {


std::optional<llvm::APSInt> bitsLiteralInteger(StaticExprAttr expression) {
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
bool bitsLiteralPositive(const llvm::APSInt &value) {
  return !value.isNegative() && !value.isZero();
}

LogicalResult bitsPrepayClosedWidth(
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
    if (valid && value && failed(bitsPrepayClosedWidth(
                             value, site, budget, depth + 1, repetitions)))
      valid = false;
  };
  auto type = [&](Type value) {
    if (valid && value && failed(bitsPrepayClosedWidth(
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

FailureOr<bool> bitsWidthsEquivalent(Operation *site, BitsType lhs, BitsType rhs) {
  if (areEquivalentHardwareTypes(lhs, rhs)) return true;
  detail::SourceCheckBudget budget(HardwareSourceCheckLimits{});
  budget.setPhase("closed Bits width comparison");
  detail::SourceCheckBudget::RawDiagnosticScope rawDiagnostics(budget);
  if (failed(bitsPrepayClosedWidth(Type(lhs), site, budget)) ||
      failed(bitsPrepayClosedWidth(Type(rhs), site, budget)))
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

LogicalResult bitsRequireSameWidth(Operation *operation, BitsType lhs, BitsType rhs,
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

bool bitsOpcodeClosed(StringRef value, ArrayRef<StringRef> choices) {
  return llvm::is_contained(choices, value);
}
LogicalResult bitsRequireOneBit(BitsType type, Operation *operation,
                            StringRef description) {
  auto width = literalInteger(type.getWidth());
  if (!width || *width != 1)
    return operation->emitOpError()
           << description << " requires canonical literal width one";
  return success();
}


} // namespace detail
} // namespace acir::ac

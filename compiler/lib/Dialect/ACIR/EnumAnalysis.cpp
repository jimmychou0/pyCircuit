// Enum value resolution and verification (B2 step-2 design r9): the
// HardwareAnalysis members and the value-verification entry live on the
// analysis side; the op-local verifiers stay in ACIREnumOps.cpp.
#include "pycircuit/Dialect/ACIR/HardwareAnalysis.h"
#include "pycircuit/Dialect/ACIR/ACIROps.h"

using namespace mlir;

namespace acir::ac {
namespace detail {

LogicalResult verifyEnumValueLocally(Operation *operation) {
  auto package = operation->getParentOfType<mlir::ModuleOp>();
  if (!package)
    return operation->emitOpError() << "enum value requires package placement";
  HardwareBindings bindings;
  bindings.owner = operation->getParentOfType<ModuleOp>();
  return HardwareAnalysis(package).verifyEnumValueOperation(operation,
                                                            bindings);
}
} // namespace detail

EnumOp HardwareAnalysis::lookupEnum(EnumType type) const {
  return dyn_cast_or_null<EnumOp>(lookupDefinition(type.getName().getValue()));
}

FailureOr<EnumDefinitionView>
HardwareAnalysis::resolveEnum(EnumType type, Operation *site) const {
  auto declaration = lookupEnum(type);
  if (!declaration)
    return site->emitOpError() << "cannot resolve nominal enum declaration '"
                               << type.getName().getValue() << "'";
  return validateEnum(declaration, site);
}

LogicalResult
HardwareAnalysis::verifyEnumValueOperation(Operation *operation,
                                           const HardwareBindings &bindings,
                                           bool requireResolved) const {
  auto width = [&](Type type, uint64_t expected,
                   StringRef description) -> LogicalResult {
    auto bits = dyn_cast<BitsType>(type);
    if (!bits)
      return operation->emitOpError() << description << " requires bits type";
    auto resolved = resolveType(bits, bindings, operation);
    if (failed(resolved))
      return failure();
    bits = cast<BitsType>(*resolved);
    if (!isStaticEvaluable(bits.getWidth(), bindings)) {
      if (requireResolved || !operation->getParentOfType<ModuleOp>())
        return operation->emitOpError()
               << description << " requires a resolved finite width";
      // The owning module's existing scope verifier checks every reference.
      // Each closed occurrence must call this verifier with requireResolved.
      return success();
    }
    auto actual = getPackedWidth(bits, bindings, operation);
    if (failed(actual))
      return failure();
    if (*actual != expected)
      return operation->emitOpError()
             << description << " width must equal " << expected;
    return success();
  };
  auto definition = [&](Type type) -> FailureOr<EnumDefinitionView> {
    auto nominal = dyn_cast<EnumType>(type);
    if (!nominal)
      return operation->emitOpError()
             << "enum operation requires an explicit nominal enum type";
    return resolveEnum(nominal, operation);
  };

  if (auto create = dyn_cast<EnumCreateOp>(operation)) {
    auto view = definition(create.getResult().getType());
    if (failed(view))
      return failure();
    for (const EnumMember &member : view->members)
      if (member.name == create.getMemberAttr())
        return success();
    return operation->emitOpError()
           << "unknown enum member '" << create.getMember() << "'";
  }
  if (auto toBits = dyn_cast<EnumToBitsOp>(operation)) {
    auto view = definition(toBits.getInput().getType());
    if (failed(view))
      return failure();
    return width(toBits.getResult().getType(), view->width,
                 "enum.to_bits result");
  }
  if (auto fromBits = dyn_cast<EnumFromBitsOp>(operation)) {
    auto view = definition(fromBits.getValue().getType());
    if (failed(view) || failed(width(fromBits.getInput().getType(), view->width,
                                     "enum.from_bits input")))
      return failure();
    return width(fromBits.getMember().getType(), 1,
                 "enum.from_bits membership result");
  }
  return success();
}

} // namespace acir::ac

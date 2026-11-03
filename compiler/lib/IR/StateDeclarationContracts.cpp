#include "IR/StateDeclarationContracts.h"

#include "Dialect/ACIR/ACIRSourceContracts.h"
#include "pycircuit/Dialect/ACIR/ACIROps.h"
#include "pycircuit/Dialect/ACIR/HardwareAnalysis.h"
#include "pycircuit/Dialect/ACIR/SourceIdentifier.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Operation.h"
#include "llvm/ADT/APSInt.h"

using namespace mlir;

namespace acir {

LogicalResult
verifyNamedLogicalDeclaration(Operation *declaration, StringAttr instanceName) {
  auto module = dyn_cast_or_null<ac::ModuleOp>(declaration->getParentOp());
  if (!module || !instanceName)
    return declaration->emitOpError()
           << "requires named direct module placement";
  if (failed(ac::detail::verifyPythonAstIdentifier(
          instanceName.getValue(), true,
          [&] { return declaration->emitOpError(); })))
    return failure();
  auto error = [&] { return declaration->emitOpError(); };
  auto identity = declaration->getAttrOfType<DictionaryAttr>("occurrence");
  if (failed(ac::detail::verifyOccurrence(identity, error)))
    return failure();
  auto symbol = module->getAttrOfType<StringAttr>("sym_name");
  if (!symbol)
    return error() << "logical state declaration requires a named owning module";
  return ac::detail::verifyOriginDefinition(
      identity, FlatSymbolRefAttr::get(declaration->getContext(),
                                       symbol.getValue()),
      "logical state declaration", error);
}

LogicalResult verifyRoutedLogicalStatePlacement(Operation *operation) {
  auto owner = dyn_cast_or_null<ac::ModuleOp>(operation->getParentOp());
  auto package =
      owner ? dyn_cast_or_null<mlir::ModuleOp>(owner->getParentOp())
            : mlir::ModuleOp();
  auto stage = package ? package->getAttrOfType<StringAttr>("ac.stage")
                       : StringAttr();
  auto contract =
      package ? package->getAttrOfType<IntegerAttr>("ac.routed_contract")
              : IntegerAttr();
  if (!owner || !package || !stage || stage.getValue() != "routed" ||
      package->hasAttr("ac.unit_kind") || !contract ||
      !contract.getType().isSignlessInteger(64) ||
      (contract.getValue() != 4 && contract.getValue() != 5))
    return operation->emitOpError()
           << "requires direct module graph placement in a versioned routed "
              "package";
  return success();
}

LogicalResult verifyLiteralControlWidth(Operation *site, Value control,
                                        StringRef description) {
  auto bits = dyn_cast<ac::BitsType>(control.getType());
  if (!bits)
    return site->emitOpError() << description << " requires width-one Bits";
  auto tree = bits.getWidth().getTree();
  auto kind = tree.getAs<StringAttr>("kind");
  if (!kind || kind.getValue() != "literal")
    return success();
  auto literal = tree.getAs<DictionaryAttr>("value");
  auto literalKind = literal ? literal.getAs<StringAttr>("kind") : StringAttr();
  if (!literalKind || literalKind.getValue() != "integer")
    return success();
  auto width = literal.getAs<ac::MathIntAttr>("value");
  if (!width || llvm::APSInt(width.getCanonicalValue()) != 1)
    return site->emitOpError() << description << " requires width-one Bits";
  return success();
}

LogicalResult verifyPayloadConstraintShape(Operation *site, Attribute raw,
                                           Type payload) {
  auto value = dyn_cast_or_null<DictionaryAttr>(raw);
  auto kind = value ? value.getAs<StringAttr>("kind") : StringAttr();
  auto type = value ? value.getAs<TypeAttr>("type") : TypeAttr();
  auto sourceKind = value ? value.getAs<StringAttr>("source_kind")
                          : StringAttr();
  if (!kind || kind.getValue() != "hardware" || !type || !sourceKind ||
      !ac::areEquivalentHardwareTypes(type.getValue(), payload))
    return site->emitOpError()
           << "state payload hardware constraint disagrees with its "
              "physical type";
  StringRef source = sourceKind.getValue();
  bool logical = source == "boolean" || source == "integer";
  if (value.size() != (logical ? 4 : 3))
    return site->emitOpError()
           << "state payload hardware constraint fields are invalid";
  if (logical || source == "fixed_bits") {
    if (!isa<ac::BitsType>(payload))
      return site->emitOpError()
             << "state payload bit or logical constraint requires BitsType";
  } else if (source == "nominal") {
    if (!isa<ac::StructType, ac::EnumType>(payload))
      return site->emitOpError()
             << "state payload nominal constraint requires a struct or enum";
  } else if (source == "table") {
    auto table = dyn_cast<ac::TableType>(payload);
    if (!table || table.getShape().size() != 1 ||
        !isa<ac::BitsType, ac::StructType, ac::EnumType>(
            table.getElementType()))
      return site->emitOpError()
             << "state payload table constraint requires one dimension of "
                "bits or nominal elements";
  } else {
    return site->emitOpError()
           << "unsupported state payload constraint source kind";
  }
  return success();
}

} // namespace acir

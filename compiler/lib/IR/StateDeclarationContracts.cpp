#include "IR/StateDeclarationContracts.h"

#include "Dialect/ACIR/ACIRSourceContracts.h"
#include "pycircuit/Interfaces/ModuleChildInterfaces.h"
#include "pycircuit/Dialect/ACIR/ACIRTypes.h"
#include "pycircuit/Dialect/ACIR/HardwareAnalysis.h"
#include "pycircuit/Dialect/ACIR/SourceIdentifier.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Operation.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/STLExtras.h"

using namespace mlir;

namespace acir::ac::detail {
Attribute semanticAttr(Attribute attr) {
  if (auto expr = dyn_cast_or_null<StaticExprAttr>(attr))
    return semanticAttr(expr.getTree());
  if (auto dict = dyn_cast_or_null<DictionaryAttr>(attr)) {
    NamedAttrList fields;
    for (NamedAttribute field : dict)
      if (field.getName() != "origin" && field.getName() != "location")
        fields.append(field.getName(), semanticAttr(field.getValue()));
    return fields.getDictionary(attr.getContext());
  }
  if (auto array = dyn_cast_or_null<ArrayAttr>(attr)) {
    SmallVector<Attribute> fields;
    for (auto value : array)
      fields.push_back(semanticAttr(value));
    return ArrayAttr::get(attr.getContext(), fields);
  }
  return attr;
}
} // namespace acir::ac::detail

namespace acir::ac {
bool areEquivalentHardwareTypes(Type lhs, Type rhs) {
  if (lhs == rhs)
    return true;
  auto lt = dyn_cast<ac::TableType>(lhs), rt = dyn_cast<ac::TableType>(rhs);
  if (lt && rt)
    return detail::semanticAttr(lt.getShape()) ==
               detail::semanticAttr(rt.getShape()) &&
           areEquivalentHardwareTypes(lt.getElementType(), rt.getElementType());
  auto l = dyn_cast<ac::BitsType>(lhs), r = dyn_cast<ac::BitsType>(rhs);
  return l && r && detail::semanticAttr(l.getWidth()) ==
                      detail::semanticAttr(r.getWidth());
}
} // namespace acir::ac

namespace acir::ac::detail {
LogicalResult verifyHardwareTypeScope(Type type, Operation *owner);
LogicalResult verifyHardwareAttributeScope(Attribute attr, Operation *owner) {
  // A closed Occurrence contains only sites and static values, never hardware
  // parameter/type references. Its intrinsic proof supplies no owner authority.
  if (auto occurrence = dyn_cast_or_null<DictionaryAttr>(attr);
      occurrence && ((occurrence.size() == 2 &&
                      isVerifiedOccurrenceGrammar(occurrence)) ||
                     (occurrence.size() == 5 &&
                      isVerifiedSourceSpanGrammar(occurrence))))
    return success();
  if (auto expr = dyn_cast_or_null<StaticExprAttr>(attr)) {
    auto tree = expr.getTree();
    // A verified literal contains only closed StaticValue grammar: it cannot
    // carry a hardware formal or a type_width reference.
    if (auto kind = tree.getAs<StringAttr>("kind");
        kind && kind.getValue() == "literal" &&
        isVerifiedStaticExpressionGrammar(tree))
      return success();
    auto tag = tree.getAs<StringAttr>("kind");
    if (tag && tag.getValue() == "reference") {
      auto ref = tree.getAs<DictionaryAttr>("ref");
      auto kind = ref ? ref.getAs<StringAttr>("kind") : StringAttr();
      auto symbol =
          ref ? ref.getAs<FlatSymbolRefAttr>("owner") : FlatSymbolRefAttr();
      auto name = ref ? ref.getAs<StringAttr>("name") : StringAttr();
      bool declared = false;
      if (auto parameters = owner->getAttrOfType<ArrayAttr>("parameters"))
        for (auto raw : parameters)
          if (auto parameter = dyn_cast<DictionaryAttr>(raw))
            if (parameter.getAs<StringAttr>("name") == name)
              declared = true;
      if (!kind || kind.getValue() != "parameter" || !symbol ||
          symbol.getValue() != SymbolTable::getSymbolName(owner).getValue() ||
          !declared)
        return owner->emitOpError() << "hardware static reference must name an "
                                       "integer formal of its owning module";
      return success();
    }
    return verifyHardwareAttributeScope(tree, owner);
  }
  if (auto dict = dyn_cast_or_null<DictionaryAttr>(attr)) {
    for (auto field : dict)
      if (field.getName() != "origin" && field.getName() != "location" &&
          failed(verifyHardwareAttributeScope(field.getValue(), owner)))
        return failure();
  }
  if (auto array = dyn_cast_or_null<ArrayAttr>(attr))
    for (auto element : array)
      if (failed(verifyHardwareAttributeScope(element, owner)))
        return failure();
  if (auto type = dyn_cast_or_null<TypeAttr>(attr))
    return verifyHardwareTypeScope(type.getValue(), owner);
  return success();
}
LogicalResult verifyHardwareTypeScope(Type type, Operation *owner) {
  if (auto table = dyn_cast<TableType>(type)) {
    if (failed(verifyHardwareAttributeScope(table.getShape(), owner)))
      return failure();
    return verifyHardwareTypeScope(table.getElementType(), owner);
  }
  if (auto formal = dyn_cast<TypeParamType>(type)) {
    auto parameters = owner->getAttrOfType<ArrayAttr>("type_parameters");
    auto symbol = SymbolTable::getSymbolName(owner);
    if (!parameters || !symbol ||
        formal.getOwner().getValue() != symbol.getValue() ||
        !llvm::is_contained(parameters, Attribute(formal.getName())))
      return owner->emitOpError()
             << "wire type parameter must belong to its declaring module";
  }
  if (auto bits = dyn_cast<BitsType>(type))
    return verifyHardwareAttributeScope(bits.getWidth(), owner);
  return success();
}

} // namespace acir::ac::detail

namespace acir::ac {

FailureOr<ArrayRef<Attribute>> structFieldList(Operation *scope,
                                               SymbolRefAttr symbol) {
  // Declaration rows read from the attribute; no concrete op class.
  auto *declaration = SymbolTable::lookupSymbolIn(scope, symbol);
  if (!declaration ||
      declaration->getName().getStringRef() != "ac.struct")
    return failure();
  auto fields = declaration->getAttrOfType<ArrayAttr>("fields");
  if (!fields)
    return failure();
  return fields.getValue();
}

FailureOr<ArrayAttr> enumMemberList(Operation *scope, SymbolRefAttr symbol) {
  auto *declaration = SymbolTable::lookupSymbolIn(scope, symbol);
  if (!declaration || declaration->getName().getStringRef() != "ac.enum")
    return failure();
  return declaration->getAttrOfType<ArrayAttr>("members");
}

FailureOr<uint64_t> closedPayloadWidth(Type payload, Operation *site) {
  auto plain = [&](StaticExprAttr expression) -> FailureOr<uint64_t> {
    // Closed literal only: symbolic formals defer to the staged tiers.
    auto tree = expression.getTree();
    auto kind = tree.getAs<StringAttr>("kind");
    if (!kind || kind.getValue() != "literal")
      return failure();
    auto literal = tree.getAs<DictionaryAttr>("value");
    auto width = literal ? literal.getAs<ac::MathIntAttr>("value") : MathIntAttr();
    llvm::APSInt widthValue(width.getCanonicalValue());
    if (!width || widthValue.isNegative() || !widthValue.isIntN(63))
      return site->emitOpError()
             << "closed payload width must be a nonnegative finite integer";
    return uint64_t(llvm::APSInt(width.getCanonicalValue()).getZExtValue());
  };
  if (auto bits = dyn_cast<ac::BitsType>(payload))
    return plain(bits.getWidth());
  if (auto table = dyn_cast<ac::TableType>(payload)) {
    // One-dimensional tables of closed payloads carry width*extent.
    uint64_t extent = 1;
    for (Attribute raw : table.getShape()) {
      auto dimension = dyn_cast<StaticExprAttr>(raw);
      if (!dimension)
        return failure();
      auto size = plain(dimension);
      if (failed(size))
        return failure();
      extent *= *size;
    }
    auto element = closedPayloadWidth(table.getElementType(), site);
    if (failed(element))
      return failure();
    return *element * extent;
  }
  if (auto enumeration = dyn_cast<ac::EnumType>(payload)) {
    auto *declaration = SymbolTable::lookupSymbolIn(
        site->getParentOfType<mlir::ModuleOp>(),
        FlatSymbolRefAttr::get(enumeration.getName()));
    if (!declaration || declaration->getName().getStringRef() != "ac.enum")
      return failure();
    auto width = declaration->getAttrOfType<ac::MathIntAttr>("width");
    if (!width)
      return failure();
    return uint64_t(llvm::APSInt(width.getCanonicalValue()).getZExtValue());
  }
  if (auto structure = dyn_cast<ac::StructType>(payload)) {
    auto *declaration = SymbolTable::lookupSymbolIn(
        site->getParentOfType<mlir::ModuleOp>(),
        FlatSymbolRefAttr::get(structure.getName()));
    if (!declaration || declaration->getName().getStringRef() != "ac.struct")
      return failure();
    uint64_t total = 0;
    for (Attribute raw : declaration->getAttrOfType<ArrayAttr>("fields")) {
      auto field = dyn_cast<DictionaryAttr>(raw);
      auto type = field ? field.getAs<TypeAttr>("type") : TypeAttr();
      if (!type)
        return failure();
      auto width = closedPayloadWidth(type.getValue(), site);
      if (failed(width))
        return failure();
      total += *width;
    }
    return total;
  }
  return failure();
}

LogicalResult verifyLiteralWidthResolves(Operation *site, Value carrier,
                                         StringRef description) {
  auto bits = dyn_cast<ac::BitsType>(carrier.getType());
  if (!bits)
    return site->emitOpError() << description << " requires a Bits carrier";
  auto tree = bits.getWidth().getTree();
  auto kind = tree.getAs<StringAttr>("kind");
  if (kind && kind.getValue() == "literal") {
    auto literal = tree.getAs<DictionaryAttr>("value");
    auto width = literal ? literal.getAs<ac::MathIntAttr>("value") : MathIntAttr();
    llvm::APSInt widthValue(width.getCanonicalValue());
    if (!width || widthValue.isNegative() || !widthValue.isIntN(63))
      return site->emitOpError()
             << description << " requires a definite nonnegative width";
  }
  // Non-literal (symbolic) widths defer to the staged tiers.
  return success();
}

} // namespace acir::ac

namespace acir {

LogicalResult
verifyNamedLogicalDeclaration(Operation *declaration, StringAttr instanceName) {
  auto module =
      dyn_cast_or_null<OwningModuleOpInterface>(declaration->getParentOp());
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
  auto symbol = module ? module.getOwnerSymName() : StringAttr();
  if (!symbol)
    return error() << "logical state declaration requires a named owning module";
  return ac::detail::verifyOriginDefinition(
      identity, FlatSymbolRefAttr::get(declaration->getContext(),
                                       symbol.getValue()),
      "logical state declaration", error);
}

LogicalResult verifyRoutedLogicalStatePlacement(Operation *operation) {
  auto owner = dyn_cast_or_null<OwningModuleOpInterface>(
      operation->getParentOp());
  auto package = owner ? dyn_cast_or_null<mlir::ModuleOp>(
                             owner->getParentOp())
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

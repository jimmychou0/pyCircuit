// Struct declaration/value verifiers (B2 step-2 design r9 §5): native
// bodies keep declaration grammar (placement, field-name uniqueness);
// the packed-width self-check and nominal field/value checks run at the
// staged entries (HardwareAnalysis::verify via --ac-verify-hardware and
// the preflight's verifyResolvedOperation walk).
#include "pycircuit/Dialect/ACIR/ACIROps.h"
#include "ACIRSourceContracts.h"
#include "llvm/ADT/StringSet.h"

using namespace mlir;

namespace acir::ac {

LogicalResult StructOp::verify() {
  if ((*this)->hasAttr("constructor"))
    return emitOpError() << "retired struct constructor binding is unsupported "
                            "on packed hardware structs";
  if (!isa<mlir::ModuleOp>((*this)->getParentOp()))
    return emitOpError() << "struct requires package placement";
  llvm::StringSet<> names;
  if (getFields().empty())
    return emitOpError() << "packed struct requires at least one field";
  for (Attribute raw : getFields()) {
    auto field = cast<DictionaryAttr>(raw);
    if (!names.insert(field.getAs<StringAttr>("name").getValue()).second)
      return emitOpError() << "Struct fields require unique names";
  }
  return success();
}

LogicalResult StructCreateOp::verify() { return success(); }

LogicalResult StructGetOp::verify() { return success(); }
} // namespace acir::ac

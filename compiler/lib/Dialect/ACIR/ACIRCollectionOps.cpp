#include "ACIRSourceContracts.h"
#include "pycircuit/Dialect/ACIR/HardwareAnalysis.h"
#include "llvm/ADT/STLExtras.h"
#include <limits>
#include <optional>
using namespace mlir;
namespace acir::ac {
namespace {
Attribute semantic(Attribute attr) {
  if (auto expr = dyn_cast_or_null<StaticExprAttr>(attr))
    return semantic(expr.getTree());
  if (auto dict = dyn_cast_or_null<DictionaryAttr>(attr)) {
    NamedAttrList fields;
    for (auto field : dict)
      if (field.getName() != "origin" && field.getName() != "location")
        fields.append(field.getName(), semantic(field.getValue()));
    return fields.getDictionary(attr.getContext());
  }
  if (auto array = dyn_cast_or_null<ArrayAttr>(attr)) {
    SmallVector<Attribute> out;
    for (auto item : array)
      out.push_back(semantic(item));
    return ArrayAttr::get(attr.getContext(), out);
  }
  return attr;
}
bool equivalentTypes(TypeRange lhs, TypeRange rhs) {
  if (lhs.size() != rhs.size())
    return false;
  for (auto [left, right] : llvm::zip(lhs, rhs))
    if (!areEquivalentHardwareTypes(left, right))
      return false;
  return true;
}
uint64_t product(ArrayRef<uint64_t> shape) {
  uint64_t n = 1;
  for (auto x : shape)
    n *= x;
  return n;
}
uint64_t indexWidth(uint64_t n) {
  return std::max<uint64_t>(1, llvm::APInt(64, n - 1).getActiveBits());
}
uint64_t countWidth(uint64_t n) {
  return std::max<uint64_t>(1, llvm::APInt(64, n).getActiveBits());
}
HardwareBindings scope(Operation *op) {
  HardwareBindings b;
  b.owner = op->getParentOfType<ModuleOp>();
  return b;
}
LogicalResult scalarRegion(Operation *op, TypeRange args, TypeRange results) {
  auto &region = op->getRegion(0);
  if (!region.hasOneBlock() || region.front().empty() ||
      !equivalentTypes(region.front().getArgumentTypes(), args) ||
      !isa<YieldOp>(region.front().back()))
    return op->emitOpError() << "requires isolated single block with matching "
                                "scalar arguments and ac.yield";
  if (!equivalentTypes(region.front().back().getOperandTypes(), results))
    return op->emitOpError()
           << "scalar region yield types do not match results";
  for (auto &nested : region.front().without_terminator())
    if (!isa<BitsConstantOp, BitsConstant4Op, BitsUnaryOp, BitsBinaryOp,
             BitsCompareOp, BitsSelectOp, BitsConcatOp, BitsExtractOp,
             BitsResizeOp, StructCreateOp, StructGetOp, EnumCreateOp,
             EnumToBitsOp, EnumFromBitsOp, ValueMergeOp>(nested) &&
        !(isa<TableMapOp>(op) &&
          isa<TableCreateOp, TableSplatOp, TableReadOp, TableGetOp, TableMapOp>(nested)))
      return nested.emitOpError()
             << "table region accepts only closed pure scalar operations";
  return success();
}
FailureOr<Type> fieldType(const HardwareAnalysis &a, Type type, ArrayAttr path,
                          const HardwareBindings &b, Operation *site) {
  auto resolved = a.resolveType(type, b, site);
  if (failed(resolved))
    return failure();
  type = *resolved;
  for (auto raw : path) {
    auto name = dyn_cast<StringAttr>(raw);
    auto record = dyn_cast<StructType>(type);
    if (!name || name.getValue().empty() || !record)
      return site->emitOpError()
             << "merge path requires existing struct fields";
    auto decl = a.lookupStruct(record);
    bool found = false;
    for (auto rawField : decl.getFields()) {
      auto field = cast<DictionaryAttr>(rawField);
      if (field.getAs<StringAttr>("name") == name) {
        type = field.getAs<TypeAttr>("type").getValue();
        found = true;
        break;
      }
    }
    if (!found)
      return site->emitOpError() << "merge path names unknown field";
  }
  return a.resolveType(type, b, site);
}
bool prefix(ArrayAttr a, ArrayAttr b) {
  return a.size() <= b.size() && std::equal(a.begin(), a.end(), b.begin());
}
FailureOr<llvm::APSInt> integerStatic(const HardwareAnalysis &a, Attribute raw,
                                      const HardwareBindings &b,
                                      Operation *site) {
  auto expr = dyn_cast_or_null<StaticExprAttr>(raw);
  if (!expr)
    return site->emitOpError() << "view parameter requires StaticExpr";
  auto value = a.evaluateStatic(expr, b, site);
  if (failed(value))
    return failure();
  auto integer = dyn_cast<MathIntAttr>(*value);
  if (!integer)
    return site->emitOpError() << "view parameter requires integer";
  return llvm::APSInt(integer.getCanonicalValue());
}
FailureOr<int64_t> signedStatic(const HardwareAnalysis &a, Attribute raw,
                                const HardwareBindings &b, Operation *site) {
  auto n = integerStatic(a, raw, b, site);
  if (failed(n))
    return failure();
  if (!n->isSignedIntN(64))
    return site->emitOpError() << "view parameter exceeds signed 64 bits";
  return n->getExtValue();
}
unsigned signedIntegerWidth(const llvm::APSInt &value) {
  // APInt signed remainder needs a zero sign bit for nonnegative APSInt
  // values whose highest stored bit is one.
  return value.getBitWidth() + unsigned(value.isUnsigned());
}
llvm::APInt extendInteger(const llvm::APSInt &value, unsigned bits) {
  return value.isUnsigned() ? value.zextOrTrunc(bits) : value.sextOrTrunc(bits);
}
FailureOr<uint64_t> rotateOffset(const HardwareAnalysis &a, Attribute raw,
                                 uint64_t extent, const HardwareBindings &b,
                                 Operation *site) {
  auto n = integerStatic(a, raw, b, site);
  if (failed(n))
    return failure();
  unsigned bits = std::max<unsigned>(65, signedIntegerWidth(*n));
  llvm::APInt modulus(bits, extent);
  auto remainder = extendInteger(*n, bits).srem(modulus);
  if (remainder.isNegative())
    remainder += modulus;
  return remainder.getZExtValue();
}
FailureOr<SmallVector<llvm::APSInt>> exactVector(const HardwareAnalysis &a,
                                                 Attribute raw,
                                                 const HardwareBindings &b,
                                                 Operation *site) {
  auto array = dyn_cast_or_null<ArrayAttr>(raw);
  if (!array)
    return site->emitOpError() << "view parameter requires StaticExpr array";
  SmallVector<llvm::APSInt> values;
  for (auto x : array) {
    auto n = integerStatic(a, x, b, site);
    if (failed(n))
      return failure();
    values.push_back(*n);
  }
  return values;
}
FailureOr<SmallVector<int64_t>> vectorStatic(const HardwareAnalysis &a,
                                             Attribute raw,
                                             const HardwareBindings &b,
                                             Operation *site) {
  auto arr = dyn_cast_or_null<ArrayAttr>(raw);
  if (!arr)
    return site->emitOpError() << "view parameter requires StaticExpr array";
  SmallVector<int64_t> values;
  for (auto x : arr) {
    auto n = signedStatic(a, x, b, site);
    if (failed(n))
      return failure();
    values.push_back(*n);
  }
  return values;
}
LogicalResult viewSchema(StringRef kind, DictionaryAttr p, unsigned rank,
                         Operation *site) {
  SmallVector<StringRef> names;
  if (kind == "reshape")
    names = {"shape"};
  else if (kind == "slice")
    names = {"offsets", "sizes", "strides"};
  else if (kind == "transpose")
    names = {"axes"};
  else if (kind == "rotate")
    names = {"axis", "offset"};
  else
    return site->emitOpError() << "unknown table view kind";
  if (p.size() != names.size())
    return site->emitOpError() << "view parameter inventory mismatch";
  for (auto name : names) {
    auto raw = p.get(name);
    if (!raw)
      return site->emitOpError() << "missing table view parameter " << name;
    if (kind == "rotate") {
      auto expr = dyn_cast<StaticExprAttr>(raw);
      if (!expr || failed(StaticExprAttr::verify(
                       [&] { return site->emitOpError(); }, expr.getTree())))
        return site->emitOpError() << "view parameter requires StaticExpr";
    } else {
      auto arr = dyn_cast<ArrayAttr>(raw);
      if (!arr || (kind != "reshape" && arr.size() != rank) || arr.empty())
        return site->emitOpError() << "view parameter rank mismatch";
      for (auto x : arr) {
        auto expr = dyn_cast<StaticExprAttr>(x);
        if (!expr || failed(StaticExprAttr::verify(
                         [&] { return site->emitOpError(); }, expr.getTree())))
          return site->emitOpError() << "view parameter requires StaticExpr";
      }
    }
  }
  return success();
}
FailureOr<SmallVector<uint64_t>>
viewShape(const HardwareAnalysis &a, StringRef kind, DictionaryAttr p,
          ArrayRef<uint64_t> in, const HardwareBindings &b, Operation *site) {
  SmallVector<uint64_t> out(in);
  if (kind == "reshape") {
    auto shape = a.resolveShape(cast<ArrayAttr>(p.get("shape")), b, site);
    if (failed(shape))
      return failure();
    if (product(*shape) != product(in))
      return site->emitOpError() << "reshape must preserve element count";
    return *shape;
  }
  if (kind == "slice") {
    auto offsets = exactVector(a, p.get("offsets"), b, site);
    auto sizes = exactVector(a, p.get("sizes"), b, site);
    auto strides = exactVector(a, p.get("strides"), b, site);
    if (failed(offsets) || failed(sizes) || failed(strides))
      return failure();
    for (unsigned i = 0; i < in.size(); ++i) {
      auto &offset = (*offsets)[i], &size = (*sizes)[i],
           &stride = (*strides)[i];
      if (offset.isNegative() || size.isNegative() || size.isZero() ||
          stride.isNegative() || stride.isZero())
        return site->emitOpError() << "slice requires nonnegative offsets and "
                                      "positive sizes/strides";
      unsigned bits = std::max({offset.getBitWidth(), size.getBitWidth(),
                                stride.getBitWidth(), 65u}) *
                          2 +
                      1;
      auto last = extendInteger(offset, bits) +
                  (extendInteger(size, bits) - llvm::APInt(bits, 1)) *
                      extendInteger(stride, bits);
      if (last.uge(llvm::APInt(bits, in[i])))
        return site->emitOpError() << "slice exceeds table extent";
      out[i] = size.getZExtValue();
    }
  } else if (kind == "transpose") {
    auto axes = vectorStatic(a, p.get("axes"), b, site);
    if (failed(axes))
      return failure();
    SmallVector<bool> seen(in.size(), false);
    for (unsigned i = 0; i < in.size(); ++i) {
      auto axis = (*axes)[i];
      if (axis < 0 || uint64_t(axis) >= in.size() || seen[axis])
        return site->emitOpError()
               << "transpose axes must be a rank permutation";
      seen[axis] = true;
      out[i] = in[axis];
    }
  } else if (kind == "rotate") {
    auto axis = signedStatic(a, p.get("axis"), b, site);
    if (failed(axis))
      return failure();
    if (*axis < 0 || uint64_t(*axis) >= in.size())
      return site->emitOpError() << "rotate axis out of range";
    if (failed(rotateOffset(a, p.get("offset"), in[*axis], b, site)))
      return failure();
  }
  return out;
}
} // namespace
} // namespace acir::ac

#include "ACIRSourceContracts.h"
#include "pycircuit/Analysis/LogicalStateValidation.h"
#include "IR/StateDeclarationContracts.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/IR/Verifier.h"
#include "pycircuit/Dialect/ACIR/HardwareAnalysis.h"
#include "pycircuit/Dialect/ACIR/SourceEffects.h"
#include "pycircuit/Dialect/ACIR/SourceIdentifier.h"
#include "pycircuit/Dialect/ACIR/SourceMethodAnalysis.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/PointerUnion.h"
#include <limits>
#include <functional>
#include "llvm/ADT/APSInt.h"
#include <type_traits>

using namespace mlir;

namespace acir::ac {
namespace {

// Single-implementation delegates to the shared logical-state validation
// helpers; call sites keep their historical names.
HardwareBindings scope(Operation *operation) {
  return stateOwnerScope(operation);
}

LogicalResult moduleOccurrence(Operation *operation, DictionaryAttr identity,
                               StringRef description) {
  auto error = [&] { return operation->emitOpError(); };
  if (failed(detail::verifyOccurrence(identity, error)))
    return failure();
  auto owner = operation->getParentOfType<ModuleOp>();
  auto symbol =
      owner ? owner->getAttrOfType<StringAttr>("sym_name") : StringAttr();
  if (!symbol)
    return error() << description << " requires a named owning module";
  return detail::verifyOriginDefinition(
      identity,
      FlatSymbolRefAttr::get(operation->getContext(), symbol.getValue()),
      description, error);
}

LogicalResult occurrence(Operation *operation) {
  return moduleOccurrence(
      operation, operation->getAttrOfType<DictionaryAttr>("occurrence"),
      "typed source operation");
}

LogicalResult namedDeclaration(Operation *operation, StringAttr name) {
  // Single shared leaf implementation for source and routed declarations.
  return verifyNamedLogicalDeclaration(operation, name);
}

LogicalResult predicate(const HardwareAnalysis &analysis, Value value,
                        const HardwareBindings &bindings, Operation *site,
                        StringRef description) {
  return stateControlPredicate(analysis, value, bindings, site, description);
}

bool equivalentTypes(TypeRange left, TypeRange right) {
  if (left.size() != right.size())
    return false;
  for (auto [lhs, rhs] : llvm::zip(left, right))
    if (!areEquivalentHardwareTypes(lhs, rhs))
      return false;
  return true;
}

LogicalResult transactionPlacement(Operation *operation, bool allowGuard,
                                   bool allowBranch = false) {
  auto *placed = operation;
  auto *parent = placed->getParentOp();
  while (allowBranch && isa_and_nonnull<SourceBranchOp>(parent)) {
    placed = parent;
    parent = placed->getParentOp();
  }
  if (!isa_and_nonnull<SourceMethodOp, SourceTransactionOp>(parent))
    return operation->emitOpError() << "requires direct method or transaction "
                                       "region placement, or legal branch arms";
  if ((!allowGuard || placed != operation) &&
      placed->getParentRegion() != &parent->getRegion(1))
    return operation->emitOpError()
           << "state writes and method calls are forbidden in guards";
  return occurrence(operation);
}

LogicalResult region(Operation *operation, unsigned ordinal, TypeRange inputs,
                     TypeRange outputs) {
  auto &region = operation->getRegion(ordinal);
  bool guard = ordinal == 0;
  if (!region.hasOneBlock() || region.front().empty() ||
      !equivalentTypes(region.front().getArgumentTypes(), inputs) ||
      !isa<YieldOp>(region.front().back()))
    return operation->emitOpError()
           << "guard/body requires one block, matching arguments and final "
              "ac.yield";
  auto &block = region.front();
  auto &yield = block.back();
  if (guard) {
    if (yield.getNumOperands() != 1)
      return yield.emitOpError() << "guard must yield exactly one predicate";
  } else if (!equivalentTypes(yield.getOperandTypes(), outputs)) {
    return yield.emitOpError() << "body yield types do not match results";
  }
  for (Operation &nested : block.without_terminator()) {
    if (isa<SourceStateReadOp>(nested))
      continue;
    if (!guard &&
        isa<SourceStateWriteOp, SourceFormalWriteOp, SourceMethodCallOp,
            SourceExpectOp, SourceObserveOp, SourceBranchOp>(nested))
      continue;
    if (!detail::isCombinationalHardwareOperation(&nested))
      return nested.emitOpError()
             << (guard ? "guard accepts only closed pure payload operations "
                         "and state reads"
                       : "body accepts only closed payload operations, state "
                         "accesses, method calls, checks and observations");
    // Pure nested collection regions do not open another effect placement.
    auto walk =
        nested.walk([&](Operation *child) {
          if (child == &nested ||
              detail::isCombinationalHardwareOperation(child) ||
              isa<YieldOp>(child))
            return WalkResult::advance();
          child->emitOpError()
              << "nested payload region cannot contain transaction "
                 "effects or declarations";
          return WalkResult::interrupt();
        });
    if (walk.wasInterrupted())
      return failure();
  }
  return success();
}

// Structured arms are the only nested regions that can carry transaction
// effects. Pure collection regions remain closed even when enclosed by a
// branch.
LogicalResult branchPayload(Operation *operation, bool arm, unsigned depth,
                            uint64_t &work) {
  if (depth > 128 || ++work > 1024 * 1024)
    return operation->emitOpError()
           << "source branch verification budget exceeded";
  if (arm && isa<SourceBranchOp>(operation)) {
    for (Region &nested : operation->getRegions())
      for (Block &block : nested)
        for (Operation &child : block)
          if (failed(branchPayload(&child, true, depth + 1, work)))
            return failure();
    return success();
  }
  if (arm && isa<SourceStateReadOp, SourceStateWriteOp>(operation)) {
    auto selectors = operation->getAttrOfType<ArrayAttr>("selectors");
    Value payload = isa<SourceStateReadOp>(operation)
                        ? operation->getResult(0)
                        : operation->getOperand(1);
    if (!selectors ||
        !isa<BitsType, EnumType, StructType, TableType>(payload.getType()))
      return operation->emitOpError()
             << "source branch arms require selected finite state accesses";
    return success();
  }
  if (arm &&
      isa<SourceFormalWriteOp, SourceMethodCallOp, SourceExpectOp,
          SourceObserveOp>(operation))
    return success();
  if (!isa<YieldOp>(operation) &&
      !detail::isCombinationalHardwareOperation(operation))
    return operation->emitOpError()
           << "source branch arms accept only closed pure payload operations, "
              "selected finite state accesses, Method calls, checks, "
              "observations "
              "and nested branches";
  for (Region &nested : operation->getRegions())
    for (Block &block : nested)
      for (Operation &child : block)
        if (failed(branchPayload(&child, false, depth + 1, work)))
          return failure();
  return success();
}


FailureOr<Type> selectedStateType(Operation *operation, ArrayAttr selectors,
                                  ValueRange indices) {
  auto owner = operation->getParentOfType<ModuleOp>();
  auto name = operation->getAttrOfType<StringAttr>("state");
  SourceStateOp state;
  if (owner && owner.getBody().hasOneBlock() && name)
    for (auto candidate : owner.getBody().front().getOps<SourceStateOp>())
      if (candidate->getAttrOfType<StringAttr>("instance_name") == name) {
        if (state)
          return operation->emitOpError() << "state reference is ambiguous";
        state = candidate;
      }
  if (!state)
    return operation->emitOpError()
           << "state reference must resolve to a state in the nearest module";
  if (state->getNumOperands() != 3 || state->getNumResults() != 1 ||
      state->getNumRegions() != 0)
    return state.emitOpError()
           << "state requires clock, reset, initial value and one old-Q result";
  if (failed(state.verifyInvariantsImpl()))
    return failure();
  return selectedPayloadType(operation, state.getQ().getType(), selectors,
                             indices);
}

} // namespace

FailureOr<Type> detail::resolveSourceBranchResultType(
    Type actualType, const HardwareAnalysis &analysis,
    const HardwareBindings &lexicalBindings, Operation *diagnosticSite,
    llvm::function_ref<LogicalResult(uint64_t, uint64_t)> charge) {
  auto error = [&] {
    return mlir::emitError(UnknownLoc::get(diagnosticSite->getContext()));
  };
  auto *owner = lexicalBindings.owner;
  auto package = analysis.getPackage();
  auto *lexical = isa<ModuleOp, ModuleImportOp>(diagnosticSite)
                      ? diagnosticSite
                      : diagnosticSite->getParentOfType<ModuleOp>().getOperation();
  if (!owner || owner != lexical || !isa<ModuleOp, ModuleImportOp>(owner) ||
      owner->getParentOp() != package.getOperation())
    return error() << "source branch result requires actual lexical bindings";
  auto ownerName = owner->getAttrOfType<StringAttr>("sym_name");
  if (!ownerName || failed(charge(1 + ownerName.getValue().size(), 0)) ||
      analysis.lookupDefinition(ownerName.getValue()) != owner)
    return error() << "source branch result bindings have a foreign owner";

  // Traverse raw metadata with an explicit, paid stack. No recursive type or
  // declaration validator runs until the complete reachable closure is paid.
  using Node = llvm::PointerUnion<Attribute, Type>;
  struct RawNode {
    Node node;
    unsigned depth;
    bool exiting = false, substituted = false, expressionTree = false;
  };
  SmallVector<RawNode, 0> raw;
  SmallVector<Node, 0> activeMetadata;
  uint64_t nodes = 0, dictionarySlots = 0, arraySlots = 0;
  uint64_t dictionaries = 0, arrays = 0, expressions = 0;
  uint64_t substitutionNodes = 0, substitutionTextBytes = 0, typeFormals = 0;
  uint64_t integerBytes = 0;
  uint64_t enumMembers = 0, enumTextBytes = 0, enumOccurrences = 0;
  uint64_t enumParseBytes = 0;
  uint64_t coldGrammarCalls = 0, fieldPathSlots = 0, shapeSlots = 0, shapeArrays = 0;
  unsigned metadataDepth = 0, nominalDepth = 0;
  auto add = [](uint64_t a, uint64_t c) {
    return c > std::numeric_limits<uint64_t>::max() - a
               ? std::numeric_limits<uint64_t>::max() : a + c;
  };
  auto multiply = [](uint64_t a, uint64_t c) {
    return c && a > std::numeric_limits<uint64_t>::max() / c
               ? std::numeric_limits<uint64_t>::max() : a * c;
  };
  auto pay = [&](uint64_t work, uint64_t bytes = 0) -> LogicalResult {
    if (work == std::numeric_limits<uint64_t>::max() ||
        bytes == std::numeric_limits<uint64_t>::max())
      return error() << "source branch result closure cost overflow";
    return charge(work, bytes);
  };
  auto reserveOne = [&](auto &values) -> LogicalResult {
    if (values.size() < values.capacity()) return success();
    using Element = typename std::decay_t<decltype(values)>::value_type;
    uint64_t requested = add(values.size(), 1);
    uint64_t capacity = std::max(requested, add(multiply(values.capacity(), 2), 1));
    if (capacity > values.max_size())
      return error() << "source branch result closure storage overflow";
    if (failed(pay(values.size(), multiply(capacity, sizeof(Element)))))
      return failure();
    values.reserve(capacity);
    return success();
  };
  auto appendRaw = [&](Node node, unsigned depth,
                       bool substituted = false,
                       bool expressionTree = false) -> LogicalResult {
    if (!node || depth > 128)
      return error() << "source branch result metadata depth exceeded";
    metadataDepth = std::max(metadataDepth, depth);
    if (failed(pay(1)) || failed(reserveOne(raw))) return failure();
    if (auto attr = node.dyn_cast<Attribute>())
      substituted |= isa<StaticExprAttr>(attr);
    raw.push_back({node, depth, false, substituted, expressionTree});
    return success();
  };
  auto metadata = [&](Node root) -> LogicalResult {
    if (failed(appendRaw(root, 0)))
      return failure();
    while (!raw.empty()) {
      auto entry = raw.pop_back_val();
      if (entry.exiting) {
        activeMetadata.pop_back();
        continue;
      }
      if (failed(pay(1 + activeMetadata.size())) ||
          failed(reserveOne(activeMetadata))) return failure();
      if (llvm::is_contained(activeMetadata, entry.node))
        return error() << "recursive source branch result metadata or nominal declaration";
      activeMetadata.push_back(entry.node);
      nodes = add(nodes, 1);
      if (entry.substituted)
        substitutionNodes = add(substitutionNodes, 1);
      if (failed(appendRaw(entry.node, entry.depth))) return failure();
      raw.back().exiting = true;
      bool valid = true;
      auto attr = [&](Attribute a) {
        bool substituted = entry.substituted;
        if (entry.expressionTree) {
          auto tree = cast<DictionaryAttr>(entry.node.get<Attribute>());
          if (a == tree.get("origin") || a == tree.get("location"))
            substituted = false;
        }
        if (valid && failed(appendRaw(Node(a), entry.depth + 1,
                                     substituted))) valid = false;
      };
      auto type = [&](Type t) {
        if (valid && failed(appendRaw(Node(t), entry.depth + 1))) valid = false;
      };
      if (auto a = entry.node.dyn_cast<Attribute>()) {
        uint64_t bytes = 0;
        if (auto text = dyn_cast<StringAttr>(a)) bytes = text.getValue().size();
        if (auto integer = dyn_cast<IntegerAttr>(a))
          bytes = (uint64_t(integer.getValue().getBitWidth()) + 7) / 8;
        if (entry.substituted) {
          substitutionTextBytes = add(substitutionTextBytes, bytes);
          if (auto dictionary = dyn_cast<DictionaryAttr>(a)) {
            dictionaries = add(dictionaries, 1);
            dictionarySlots = add(dictionarySlots, dictionary.size());
          }
          if (auto array = dyn_cast<ArrayAttr>(a)) {
            arrays = add(arrays, 1);
            arraySlots = add(arraySlots, array.size());
          }
        }
        if (isa<StaticExprAttr>(a)) expressions = add(expressions, 1);
        if (auto integer = dyn_cast<MathIntAttr>(a))
          integerBytes = add(integerBytes, integer.getCanonicalValue().size());
        if (failed(pay(1 + bytes))) return failure();
        if (auto expression = dyn_cast<StaticExprAttr>(a)) {
          auto tree = expression.getTree();
          auto kind = tree.getAs<StringAttr>("kind");
          // Match HardwareAnalysis::substitute: literal/type-width trees are
          // borrowed intact. Computational trees can be rebuilt; a reference
          // creates a literal only for an explicit non-expression binding.
          bool rebuild = kind && (kind.getValue() == "unary" ||
                                  kind.getValue() == "binary" ||
                                  kind.getValue() == "select");
          if (kind && kind.getValue() == "reference") {
            auto reference = tree.getAs<DictionaryAttr>("ref");
            auto owner = reference
                             ? reference.getAs<FlatSymbolRefAttr>("owner")
                             : FlatSymbolRefAttr();
            auto name = reference ? reference.getAs<StringAttr>("name")
                                  : StringAttr();
            if (owner && name && owner.getValue() == ownerName.getValue()) {
              auto binding = lexicalBindings.integers.find(name.getValue());
              rebuild = binding != lexicalBindings.integers.end() &&
                        !isa<StaticExprAttr>(binding->second);
            }
          }
          if (failed(appendRaw(Node(expression.getTree()), entry.depth + 1,
                               rebuild, true))) valid = false;
        } else a.walkImmediateSubElements(attr, type);
      } else {
        if (failed(pay(1))) return failure();
        Type current = entry.node.get<Type>();
        if (auto table = dyn_cast<TableType>(current)) {
          shapeSlots = add(shapeSlots, table.getShape().size());
          shapeArrays = add(shapeArrays, 1);
        }
        typeFormals += isa<TypeParamType>(current);
        current.walkImmediateSubElements(attr, type);
        Operation *declaration = nullptr;
        if (auto record = dyn_cast<StructType>(current)) {
          if (!record.getName()) return error() << "malformed nominal Struct type";
          declaration = analysis.lookupStruct(record);
        } else if (auto enumeration = dyn_cast<EnumType>(current)) {
          if (!enumeration.getName()) return error() << "malformed nominal Enum type";
          declaration = analysis.lookupEnum(enumeration);
        }
        if (isa<StructType, EnumType>(current)) {
          if (!declaration || declaration->getParentOp() != package.getOperation())
            return error() << "unresolved source branch nominal declaration";
          if (auto enumeration = dyn_cast<EnumType>(current)) {
            ++enumOccurrences;
            if (auto width = declaration->getAttrOfType<MathIntAttr>("width"))
              enumParseBytes = add(enumParseBytes, width.getCanonicalValue().size());
            auto members = declaration->getAttrOfType<ArrayAttr>("members");
            if (members) {
              if (failed(pay(members.size()))) return failure();
              enumMembers = add(enumMembers, members.size());
              for (Attribute rawMember : members)
                if (auto member = dyn_cast<DictionaryAttr>(rawMember)) {
                  if (auto name = member.getAs<StringAttr>("name"))
                    enumTextBytes = add(enumTextBytes, name.getValue().size() + 1);
                  if (auto code = member.getAs<MathIntAttr>("code")) {
                    enumTextBytes = add(enumTextBytes, code.getCanonicalValue().size() + 1);
                    enumParseBytes = add(enumParseBytes, code.getCanonicalValue().size());
                  }
                }
            }
          }
          if (failed(pay(1, declaration->getPropertiesStorageSize()))) return failure();
          // Native static substitution borrows nominal TypeAttrs; it never
          // reconstructs declaration dictionaries, arrays or locations. Keep
          // their raw traversal separate from actual substitution storage.
          for (Attribute metadata : {Attribute(declaration->getRawDictionaryAttrs()),
                                     Attribute(declaration->getLoc())})
            if (valid && failed(appendRaw(Node(metadata), entry.depth + 1)))
              valid = false;
          if (valid && declaration->getPropertiesStorageSize() &&
              failed(appendRaw(Node(declaration->getPropertiesAsAttribute()),
                               entry.depth + 1)))
            valid = false;
        }
      }
      if (!valid) return failure();
    }
    return success();
  };
  for (const auto &binding : lexicalBindings.integers) {
    if (failed(pay(1 + binding.getKey().size())) ||
        failed(metadata(Node(binding.second)))) return failure();
  }
  for (const auto &binding : lexicalBindings.types) {
    if (failed(pay(1 + binding.getKey().size())) ||
        failed(metadata(Node(binding.second)))) return failure();
  }
  // Scope checking reads these inventories. Inspect them before it can recurse.
  for (StringRef name : {"type_parameters", "parameters"})
    if (auto attr = owner->getAttr(name))
      if (failed(metadata(Node(attr)))) return failure();

  struct Frame { Type type; ArrayAttr fields; size_t next = 0; bool entered = false; };
  SmallVector<Frame, 0> stack;
  SmallVector<Type, 0> validated;
  auto push = [&](Type type) -> LogicalResult {
    if (!type || stack.size() > 128)
      return error() << "source branch result nominal depth exceeded";
    if (failed(pay(1 + stack.size())) || failed(reserveOne(stack)))
      return failure();
    for (const Frame &ancestor : stack)
      if (ancestor.type == type)
        return error() << "recursive source branch result nominal declaration";
    stack.push_back({type, {}, 0, false});
    nominalDepth = std::max(nominalDepth, unsigned(stack.size()));
    return success();
  };
  if (failed(metadata(Node(actualType))) || failed(push(actualType))) return failure();
  while (!stack.empty()) {
    auto &frame = stack.back();
    if (!frame.entered) {
      frame.entered = true;
      if (!isa<BitsType, EnumType, StructType, TableType>(frame.type))
        return error() << "source branch results require finite hardware payloads";
      Operation *declaration = nullptr;
      if (auto record = dyn_cast<StructType>(frame.type))
        declaration = analysis.lookupStruct(record);
      else if (auto enumeration = dyn_cast<EnumType>(frame.type))
        declaration = analysis.lookupEnum(enumeration);
      if (isa<EnumType, StructType>(frame.type)) {
        if (!declaration || declaration->getParentOp() != package.getOperation())
          return error() << "unresolved source branch nominal declaration";
        auto name = declaration->getAttrOfType<StringAttr>("sym_name");
        if (!name || name.getValue().empty())
          return error() << "source branch nominal declaration requires a name";
        if (isa<StructOp>(declaration)) {
          frame.fields = declaration->getAttrOfType<ArrayAttr>("fields");
          if (!frame.fields || frame.fields.empty() || declaration->hasAttr("constructor"))
            return error() << "invalid source branch Struct declaration";
          for (auto [i, raw] : llvm::enumerate(frame.fields)) {
            if (failed(verifyNominalStructField(
                    raw, analysis, declaration, diagnosticSite)))
              return failure();
            auto field = cast<DictionaryAttr>(raw);
            auto key = field.getAs<StringAttr>("name");
            if (failed(pay(i + key.getValue().size()))) return failure();
            for (Attribute previous : frame.fields.getValue().take_front(i))
              if (cast<DictionaryAttr>(previous).getAs<StringAttr>("name") == key)
                return error() << "source branch Struct field names must be unique";
          }
        } else {
          auto width = declaration->getAttrOfType<MathIntAttr>("width");
          auto encoding = declaration->getAttrOfType<StringAttr>("encoding");
          auto members = declaration->getAttrOfType<ArrayAttr>("members");
          if (!width || !encoding || !members || members.empty())
            return error() << "invalid source branch Enum declaration";
          for (Attribute raw : members) {
            auto member = dyn_cast<DictionaryAttr>(raw);
            if (!member || member.size() != 2 || !member.getAs<StringAttr>("name") ||
                !member.getAs<MathIntAttr>("code"))
              return error() << "invalid source branch Enum member";
          }
        }
      }
    }
    if (auto table = dyn_cast<TableType>(frame.type); table && frame.next == 0) {
      ++frame.next;
      if (failed(TableType::verify(error, table.getShape(), table.getElementType())) ||
          failed(push(table.getElementType()))) return failure();
      continue;
    }
    if (frame.fields && frame.next < frame.fields.size()) {
      Type child = cast<DictionaryAttr>(frame.fields[frame.next++])
                       .getAs<TypeAttr>("type").getValue();
      // Fixed Table-valued Struct fields use the existing packed layout path.
      // It resolves positive, closed dimensions after the complete closure is
      // paid below; this does not admit multidimensional or Table-of-Table
      // source fields.
      if (auto table = dyn_cast<TableType>(child))
        if (table.getShape().size() != 1 ||
            !isa<BitsType, EnumType, StructType>(table.getElementType()))
          return error() << "source branch Struct Table fields require one "
                            "dimension and Bits, Enum or Struct elements";
      fieldPathSlots = add(fieldPathSlots, stack.size());
      if (failed(push(child))) return failure();
      continue;
    }
    if (failed(pay(1)) || failed(reserveOne(validated))) return failure();
    if (auto bits = dyn_cast<BitsType>(frame.type)) {
      if (failed(pay(1))) return failure();
      auto width = bits.getWidth();
      auto tree = width ? width.getTree() : DictionaryAttr();
      coldGrammarCalls += !tree || !detail::isVerifiedStaticExpressionGrammar(tree);
    }
    validated.push_back(frame.type);
    stack.pop_back();
  }
  // Grammar validation uses inline buckets for small widths. Per container,
  // 2*inline + 8*insertions covers inline/temporary storage and cumulative heap
  // growth, including the first 64-bucket spill and active-set tombstone rebuilds.
  // A warm exact grammar key returns before those local containers exist.
  // It does not replace the owner/scope, binding or layout checks below.
  uint64_t grammarBuckets = coldGrammarCalls ? multiply(2, add(
      multiply(detail::staticGrammarInlineBuckets, coldGrammarCalls),
      multiply(4, expressions))) : 0;
  uint64_t grammarBytes = multiply(grammarBuckets,
      sizeof(std::pair<DictionaryAttr, bool>) + sizeof(DictionaryAttr));
  // Native substitution visits computational expression children and borrows
  // source origin/location trees unchanged. Those trees still paid their full
  // raw traversal above and native grammar/scope validation below. Summed temporary
  // vector allocations stay below four times the logical handle count;
  // interning stores another copy. Charge cumulative allocation, including
  // buffers freed during growth. Strings in these records remain borrowed.
  uint64_t cloneBytes = add(
      multiply(dictionarySlots, 5 * sizeof(NamedAttribute)),
      multiply(add(arraySlots, shapeSlots), 5 * sizeof(Attribute)));
  cloneBytes = add(cloneBytes, multiply(add(add(dictionaries, arrays), shapeArrays),
                                       3 * sizeof(Attribute)));
  cloneBytes = add(cloneBytes, multiply(expressions, 2 * sizeof(Attribute)));
  // resolveType substitutes once; evaluateStatic can substitute each nested
  // expression once more. Explicit binding chains add their measured length.
  uint64_t substitutionPasses = add(1 + metadataDepth,
      add(lexicalBindings.types.size(), lexicalBindings.integers.size()));
  uint64_t substitutionBytes = multiply(cloneBytes, substitutionPasses);
  // Enum resolution owns a member vector and two StringSets. Each StringMap
  // starts at 16 buckets, doubles at 3/4 occupancy, and copies its own keys.
  // Layout resolves each Enum twice; resolving a standalone root adds one.
  uint64_t enumBuckets = multiply(4, add(multiply(16, enumOccurrences),
                                         multiply(4, enumMembers)));
  uint64_t enumBytes = multiply(enumBuckets, sizeof(void *) + sizeof(unsigned));
  enumBytes = add(enumBytes, multiply(enumMembers,
      4 * sizeof(EnumMember) + 2 * sizeof(llvm::StringMapEntry<std::nullopt_t>)));
  enumBytes = add(enumBytes, enumTextBytes);
  enumBytes = multiply(enumBytes, isa<EnumType>(actualType) ? 3 : 2);
  // getPackedWidth keeps an active Type set and copies field-path handles
  // twice plus possible vector growth at each actual field edge.
  uint64_t layoutBytes = multiply(2 * (64 + 4 * uint64_t(nominalDepth)), sizeof(Type));
  layoutBytes = add(layoutBytes, multiply(fieldPathSlots, 4 * sizeof(StringAttr)));
  if (isa<BitsType>(actualType)) layoutBytes = 0;
  uint64_t traversalBytes = multiply(2 * (metadataDepth + 1),
                                     sizeof(std::pair<Attribute, Operation *>));
  // Prepay parsing/copying of existing numeric spellings separately from
  // traversal work. Arbitrary evaluated APInt result precision remains an
  // inherited evaluator limitation; source size does not bound that allocation.
  uint64_t spellingBytes = multiply(integerBytes, add(substitutionPasses, 2));
  uint64_t nativeBytes = add(add(grammarBytes, substitutionBytes),
      add(add(enumBytes, layoutBytes), add(traversalBytes, spellingBytes)));
  // Only computational expression fields undergo repeated substitution.
  // Provenance and declaration metadata retain three complete native visits
  // for grammar/scope/layout checks, in addition to the raw traversal above;
  // they no longer pay for metadata copies the evaluator does not perform.
  uint64_t substitutionVisits =
      add(add(substitutionNodes, dictionarySlots), arraySlots);
  uint64_t nativeWork = add(multiply(nodes - substitutionNodes, 3),
      add(multiply(add(substitutionVisits, shapeSlots), add(substitutionPasses, 3)),
          multiply(substitutionTextBytes, substitutionPasses)));
  // Enum layout resolves each leaf twice (three times for a standalone root).
  // Each resolution revisits members, hashes names/codes and parses numeric
  // spellings; these are distinct from expression substitution visits.
  uint64_t enumVisits = add(add(enumOccurrences, enumMembers),
                           add(enumTextBytes, enumParseBytes));
  nativeWork = add(nativeWork,
      multiply(enumVisits, isa<EnumType>(actualType) ? 3 : 2));
  nativeWork = add(nativeWork, add(validated.size(), fieldPathSlots));
  // Scope verification scans the actual owner's formal inventories at each
  // reference. Counting every expression as a parameter reference is a safe
  // bound and retains the cost of repeated owner/name comparisons above.
  auto parameters = owner->getAttrOfType<ArrayAttr>("parameters");
  auto typeParameters = owner->getAttrOfType<ArrayAttr>("type_parameters");
  nativeWork = add(nativeWork,
      add(multiply(expressions, parameters ? parameters.size() : 0),
          multiply(typeFormals, typeParameters ? typeParameters.size() : 0)));
  if (failed(pay(nativeWork, nativeBytes))) return failure();
  for (Type type : validated) {
    if (auto bits = dyn_cast<BitsType>(type)) {
      if (failed(BitsType::verify(error, bits.getWidth()))) return failure();
    } else if (auto enumeration = dyn_cast<EnumType>(type)) {
      if (failed(EnumType::verify(error, enumeration.getName()))) return failure();
    } else if (auto record = dyn_cast<StructType>(type)) {
      if (failed(StructType::verify(error, record.getName()))) return failure();
    }
    if (failed(detail::verifyHardwareTypeScope(type, owner))) return failure();
  }
  if (failed(detail::verifySourceTemplateType(
          analysis, actualType, lexicalBindings, diagnosticSite))) return failure();
  return analysis.resolveType(actualType, lexicalBindings, diagnosticSite);
}

LogicalResult SourceStateOp::verify() {
  auto package = (*this)->getParentOfType<mlir::ModuleOp>();
  auto stage = package ? package->getAttrOfType<StringAttr>("ac.stage")
                       : StringAttr();
  if (stage && stage.getValue() == "final")
    return emitOpError() << "logical state is forbidden in a final package";
  if ((*this)->hasAttr("sym_name"))
    return emitOpError() << "state uses instance_name; sym_name is unsupported";
  if (failed(namedDeclaration(*this, getInstanceNameAttr())))
    return failure();
  auto owner = cast<ModuleOp>((*this)->getParentOp());
  for (Operation &other : owner.getBody().front())
    if (&other != getOperation() &&
        isa<SourceStateOp, InstanceOp, CollectionOp>(other) &&
        other.getAttrOfType<StringAttr>("instance_name") ==
            getInstanceNameAttr())
      return emitOpError() << "state instance_name must be unique across state "
                              "and child instance declarations";
  for (auto other : owner.getBody().front().getOps<SourceStateOp>()) {
    if (other == *this)
      continue;
    if (failed(occurrence(other)))
      return failure();
    if (detail::compareClosedSourceStructure(getOccurrence(),
                                             other.getOccurrence()) == 0)
      return emitOpError() << "state declarations require distinct occurrence "
                              "identities within their module";
  }
  return verifyLogicalStateSourceBinding(*this);
}

LogicalResult SourceStateReadOp::verify() {
  // Path/selector semantics run during routing preparation and the staged
  // plan (r9 §5); the retired-grammar placement check stays native.
  return success();
}

LogicalResult SourceStateWriteOp::verify() {
  // Path/selector semantics run during routing preparation and the staged
  // plan (r9 §5).
  return success();
}

LogicalResult SourceFormalWriteOp::verify() {
  for (NamedAttribute attr : (*this)->getAttrs())
    if (!llvm::is_contained(ArrayRef<StringRef>{"formal", "selectors",
                                                "operandSegmentSizes",
                                                "occurrence", "ac.location"},
                            attr.getName().getValue()))
      return emitOpError() << "unknown formal write attribute";
  if (failed(transactionPlacement(*this, false, true)))
    return failure();
  auto method = (*this)->getParentOfType<SourceMethodOp>();
  if (!method)
    return emitOpError() << "formal write requires a lexical Method formal";
  auto formal = getFormalAttr();
  auto signature = method.getFunctionType();
  auto proposals = method->getAttrOfType<ArrayAttr>("ac.formal_proposals");
  if (!formal.getType().isSignlessInteger(64) || formal.getInt() < 0 ||
      static_cast<uint64_t>(formal.getInt()) >= signature.getNumInputs() ||
      !proposals || !llvm::is_contained(proposals, formal))
    return emitOpError()
           << "formal write must identify a declared writable input formal";
  if (!areEquivalentHardwareTypes(getCandidate().getType(),
                                  signature.getInput(formal.getInt())))
    return emitOpError()
           << "formal write candidate must match the whole input formal type";
  // Selected-payload checks run during routing preparation (r9 §5).
  if (bool(getPrevious()) != bool(getSnapshot()))
    return emitOpError() << "forwarded formal previous and snapshot must be present together";
  if (getPrevious() &&
      (!areEquivalentHardwareTypes(getPrevious().getType(), getCandidate().getType()) ||
       !areEquivalentHardwareTypes(getSnapshot().getType(), getCandidate().getType())))
    return emitOpError() << "forwarded formal snapshot must preserve the whole formal type";
  if (getPrevious() && (!getSelectors().empty() || !getIndices().empty()))
    return emitOpError()
           << "forwarded formal proposal must inherit callee write domains";
  auto owner = (*this)->getParentOfType<ModuleOp>();
  if (failed(detail::verifySourceEffectMetadata(
          getOccurrence(), getSourceLocation(), owner,
          [&] { return emitOpError(); })) ||
      failed(detail::verifyHardwareTypeScope(getPayload().getType(), owner)) ||
      failed(detail::verifyHardwareTypeScope(getCandidate().getType(), owner)))
    return failure();
  // Path predicate resolution runs during routing preparation (r9 §5).
  return success();
}

LogicalResult SourceBranchOp::verifyRegions() {
  return success();
}

LogicalResult SourceMethodOp::verifyRegions() {
  return success();
}

LogicalResult SourceMethodOp::verifyRegions() {
  SmallVector<Type> arguments(getFunctionType().getInputs());
  llvm::append_range(arguments, getCaptures().getTypes());
  if (failed(region(*this, 0, arguments, {})))
    return failure();
  return region(*this, 1, arguments, getFunctionType().getResults());
}

LogicalResult SourceMethodCallOp::verifySymbolUses(SymbolTableCollection &) {
  // Receiver binding resolution runs during routing preparation (r9 §5).
  return success();
}


LogicalResult SourceTransactionOp::verifyRegions() {
  return success();
}

LogicalResult SourceTransactionOp::verifyRegions() {
  if (failed(region(*this, 0, getCaptures().getTypes(), {})))
    return failure();
  if (failed(region(*this, 1, getCaptures().getTypes(), getResultTypes())))
    return failure();
  return failed(getSourceRegistrationEntry(*this)) ? failure() : success();
}

Operation *detail::findUnloweredTransaction(Operation *root) {
  SmallVector<Operation *> pending;
  if (root)
    pending.push_back(root);
  while (!pending.empty()) {
    Operation *operation = pending.pop_back_val();
    if (isa<SourceStateOp, SourceStateReadOp, SourceStateWriteOp,
            SourceFormalWriteOp, SourceMethodOp, SourceMethodCallOp, SourceTransactionOp,
            SourceBranchOp, SourceQueuePopOp, SourceQueuePeekOp,
            SourceQueuePushOp>(operation) ||
        (isa<InstanceOp>(operation) &&
         (operation->hasAttr("ac.source_unbound_push") ||
          operation->hasAttr("ac.source_unbound_pop") ||
          operation->hasAttr("ac.payload_constraint") ||
          operation->hasAttr("ac.reference_bindings"))) ||
        (isa<ModuleOp, ModuleImportOp>(operation) &&
         operation->hasAttr("ac.reference_parameters")))
      return operation;
    for (Region &region : llvm::reverse(operation->getRegions()))
      for (Block &block : llvm::reverse(region))
        for (Operation &child : llvm::reverse(block))
          pending.push_back(&child);
  }
  return nullptr;
}

LogicalResult detail::verifyNoUnloweredTransactions(Operation *root) {
  if (Operation *operation = findUnloweredTransaction(root))
    return operation->emitOpError() << "unlowered source transaction operation "
                                       "cannot cross the verified "
                                       "hardware boundary";
  return success();
}

} // namespace acir::ac

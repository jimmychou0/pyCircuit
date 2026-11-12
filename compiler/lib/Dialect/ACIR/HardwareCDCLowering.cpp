#include "HardwareSourceCheckPreflight.h"
#include "HardwareSourceChecks.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/SymbolTable.h"
#include "pycircuit/Dialect/ACIR/HardwareCDC.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/MathExtras.h"
using namespace mlir;
namespace acir::ac {
namespace {
LogicalResult verifyFinalHardwareCDCComponent(Operation *witness);
constexpr StringLiteral registerSymbol = "pycircuit.__builtins__.__cdc_dffe";
// A declarative equation graph is the selected public implementation contract.
// Producer and checker each reconstruct it from the original logical resource;
// no values or maps recorded by materialization are correspondence evidence.
struct Ref {
  Value external;
  unsigned node = 0, result = 0;
  Ref() = default;
  explicit Ref(Value value) : external(value) {}
  Ref(unsigned node, unsigned result = 0) : node(node), result(result) {}
};
struct Node {
  std::string operation;
  SmallVector<Type> types;
  NamedAttrList attributes;
  SmallVector<Ref> inputs;
};
struct Circuit {
  Operation *source;
  SmallVector<Node, 0> nodes;
  SmallVector<Ref> outputs;
};
class Equations {
  Circuit circuit;
  Builder b;
  HardwareAnalysis analysis;
  detail::SourceCheckBudget &budget;
  bool valid = true;
  BitsType bit;
  Ref zeroBit, oneBit;
  StaticExprAttr literal(uint64_t n) {
    auto provenance = bit.getWidth().getTree();
    auto number = MathIntAttr::get(b.getContext(),
                                   llvm::APSInt(llvm::APInt(64, n), true));
    return StaticExprAttr::get(
        b.getContext(),
        b.getDictionaryAttr(
            {b.getNamedAttr("kind", b.getStringAttr("literal")),
             b.getNamedAttr(
                 "value",
                 b.getDictionaryAttr(
                     {b.getNamedAttr("kind", b.getStringAttr("integer")),
                      b.getNamedAttr("value", number)})),
             b.getNamedAttr("origin", circuit.source->getAttr("occurrence")),
             b.getNamedAttr("location", provenance.get("location"))}));
  }
  Type bits(unsigned width) {
    return BitsType::get(b.getContext(), literal(width));
  }
  Ref node(StringRef name, TypeRange types, ArrayRef<Ref> inputs = {},
           ArrayRef<NamedAttribute> attrs = {}) {
    if (failed(budget.debit(128 + 32 * inputs.size(), circuit.source)))
      valid = false;
    unsigned index = circuit.nodes.size();
    Node entry;
    entry.operation = name.str();
    llvm::append_range(entry.types, types);
    llvm::append_range(entry.inputs, inputs);
    entry.attributes.assign(attrs);
    circuit.nodes.push_back(std::move(entry));
    return Ref(index);
  }
  Type type(Ref value) {
    return value.external ? value.external.getType()
                          : circuit.nodes[value.node].types[value.result];
  }
  Ref constant(Type type, uint64_t value) {
    return node(BitsConstantOp::getOperationName(), {type}, {},
                {b.getNamedAttr("value", literal(value))});
  }
  Ref binary(StringRef opcode, Ref lhs, Ref rhs) {
    return node(BitsBinaryOp::getOperationName(), {type(lhs)}, {lhs, rhs},
                {b.getNamedAttr("opcode", b.getStringAttr(opcode))});
  }
  Ref invert(Ref input) {
    return node(BitsUnaryOp::getOperationName(), {type(input)}, {input},
                {b.getNamedAttr("opcode", b.getStringAttr("not"))});
  }
  Ref equal(Ref lhs, Ref rhs, StringRef predicate = "eq") {
    return node(BitsCompareOp::getOperationName(), {bit}, {lhs, rhs},
                {b.getNamedAttr("predicate", b.getStringAttr(predicate))});
  }
  Ref slice(Ref input, unsigned width) {
    return node(BitsExtractOp::getOperationName(), {bits(width)}, {input},
                {b.getNamedAttr("low", literal(0))});
  }
  Ref merge(Ref condition, Ref yes, Ref no) {
    auto result = node(
        ValueMergeOp::getOperationName(), {type(no), bit}, {no, condition, yes},
        {b.getNamedAttr("paths", b.getArrayAttr({b.getArrayAttr({})})),
         b.getNamedAttr("operandSegmentSizes",
                        b.getDenseI32ArrayAttr({1, 1, 1}))});
    return result;
  }
  Ref zero(Type type) {
    if (auto bits = dyn_cast<BitsType>(type))
      return constant(bits, 0);
    return node(ValueZeroOp::getOperationName(), {type});
  }
  Ref storage(StringRef role, Type type, Ref clock, Ref reset, StringRef edge,
              Ref initial, bool resettable = true) {
    auto name =
        circuit.source->getAttrOfType<StringAttr>("instance_name").getValue();
    auto resourceOccurrence =
        circuit.source->getAttrOfType<DictionaryAttr>("occurrence");
    NamedAttrList occurrence(resourceOccurrence.getValue());
    NamedAttrList site(
        resourceOccurrence.getAs<DictionaryAttr>("site").getValue());
    auto originalPath = cast<ArrayAttr>(site.get("ast_path"));
    SmallVector<Attribute> path(originalPath.begin(), originalPath.end());
    path.push_back(b.getDictionaryAttr(
        {b.getNamedAttr("kind", b.getStringAttr("field")),
         b.getNamedAttr("name", b.getStringAttr(("__cdc." + role).str()))}));
    site.set("ast_path", b.getArrayAttr(path));
    occurrence.set("site", site.getDictionary(b.getContext()));
    SmallVector<NamedAttribute> attrs{
        b.getNamedAttr("instance_name",
                       b.getStringAttr((name + ".__cdc." + role).str())),
        b.getNamedAttr("callee",
                       FlatSymbolRefAttr::get(b.getContext(), registerSymbol)),
        b.getNamedAttr("parameters", b.getArrayAttr({})),
        b.getNamedAttr(
            "type_arguments",
            b.getArrayAttr({TypeAttr::get(
                isa<TableType>(type) ? cast<TableType>(type).getElementType()
                                     : type)})),
        b.getNamedAttr("occurrence", occurrence.getDictionary(b.getContext())),
        b.getNamedAttr("edge", b.getStringAttr(edge)),
        b.getNamedAttr("reset_kind",
                       b.getStringAttr(resettable ? "async" : "none")),
        b.getNamedAttr(
            "init_kind",
            b.getStringAttr(resettable ? "explicit" : "unspecified")),
        b.getNamedAttr(
            "ac.cdc_component",
            b.getDictionaryAttr(
                {b.getNamedAttr("resource",
                                circuit.source->getAttr("occurrence")),
                 b.getNamedAttr("role", b.getStringAttr(role)),
                 b.getNamedAttr("implementation", b.getI64IntegerAttr(1))}))};
    if (resettable)
      attrs.push_back(b.getNamedAttr(
          "reset_polarity", circuit.source->getAttr("reset_polarity")));
    if (auto table = dyn_cast<TableType>(type)) {
      attrs.push_back(b.getNamedAttr("shape", table.getShape()));
      auto splat = [&](Ref input) {
        return node(TableSplatOp::getOperationName(),
                    {TableType::get(b.getContext(), table.getShape(),
                                    this->type(input))},
                    {input}, {b.getNamedAttr("shape", table.getShape())});
      };
      return node(CollectionOp::getOperationName(), {type},
                  {splat(clock), splat(reset), splat(oneBit), initial, initial,
                   initial},
                  attrs);
    }
    return node(InstanceOp::getOperationName(), {type},
                {clock, reset, oneBit, initial, initial, initial}, attrs);
  }
  void drive(Ref state, Ref data, Ref enable) {
    if (auto table = dyn_cast<TableType>(type(state)))
      enable =
          node(TableSplatOp::getOperationName(),
               {TableType::get(b.getContext(), table.getShape(), type(enable))},
               {enable}, {b.getNamedAttr("shape", table.getShape())});
    circuit.nodes[state.node].inputs[2] = enable;
    circuit.nodes[state.node].inputs[3] = data;
  }
  Ref sync(StringRef role, Ref input, Ref clock, Ref reset, StringRef edge) {
    auto initial = zero(type(input));
    auto first = storage((role + ".stage1").str(), type(input), clock, reset,
                         edge, initial);
    auto second = storage((role + ".stage2").str(), type(input), clock, reset,
                          edge, initial);
    drive(first, input, oneBit);
    drive(second, first, oneBit);
    return second;
  }

public:
  Equations(Operation *source, detail::SourceCheckBudget &budget)
      : circuit{source, {}, {}}, b(source->getContext()),
        analysis(source->getParentOfType<mlir::ModuleOp>()), budget(budget) {
    bit = cast<BitsType>(source->getOperand(0).getType());
    zeroBit = constant(bit, 0);
    oneBit = constant(bit, 1);
  }
  FailureOr<Circuit> run() {
    auto *source = circuit.source;
    Ref reset(source->getOperand(2));
    if (isa<AsyncRegOp>(source) ||
        (source->getAttrOfType<StringAttr>("logical_kind") &&
         source->getAttrOfType<StringAttr>("logical_kind").getValue() ==
             "async_reg")) {
      Ref clock(source->getOperand(1));
      auto edge = source->getAttrOfType<StringAttr>("target_edge").getValue();
      auto released = sync("release", oneBit, clock, reset, edge);
      auto first = storage("data.stage1", bit, clock, reset, edge, zeroBit);
      auto second = storage("data.stage2", bit, clock, reset, edge, zeroBit);
      drive(first, merge(released, Ref(source->getOperand(3)), zeroBit),
            oneBit);
      drive(second, merge(released, first, zeroBit), oneBit);
      circuit.outputs.push_back(second);
    } else {
      auto fifo = dyn_cast<AsyncFIFOOp>(source);
      uint64_t depth = source->getAttrOfType<IntegerAttr>("depth").getInt();
      if (depth < 2 || !llvm::isPowerOf2_64(depth))
        return source->emitOpError("selected CDC FIFO implementation supports "
                                   "power-of-two depth >= 2");
      unsigned addressWidth = llvm::Log2_64(depth),
               pointerWidth = addressWidth + 1;
      auto pointerType = bits(pointerWidth);
      auto pointerZero = constant(pointerType, 0),
           pointerOne = constant(pointerType, 1);
      Ref writeClock(source->getOperand(0)), readClock(source->getOperand(1));
      auto writeEdge =
          source->getAttrOfType<StringAttr>("write_edge").getValue();
      auto readEdge = source->getAttrOfType<StringAttr>("read_edge").getValue();
      auto writeReleased =
          sync("write.release", oneBit, writeClock, reset, writeEdge);
      auto readReleased =
          sync("read.release", oneBit, readClock, reset, readEdge);
      auto peerReadReleased = sync("write.peer_release", readReleased,
                                   writeClock, reset, writeEdge);
      auto peerWriteReleased =
          sync("read.peer_release", writeReleased, readClock, reset, readEdge);
      auto wb = storage("write.binary", pointerType, writeClock, reset,
                        writeEdge, pointerZero);
      auto wg = storage("write.gray", pointerType, writeClock, reset, writeEdge,
                        pointerZero);
      auto rb = storage("read.binary", pointerType, readClock, reset, readEdge,
                        pointerZero);
      auto rg = storage("read.gray", pointerType, readClock, reset, readEdge,
                        pointerZero);
      auto syncedRead = sync("write.pointer", rg, writeClock, reset, writeEdge);
      auto syncedWrite = sync("read.pointer", wg, readClock, reset, readEdge);
      auto full = equal(
          wg, binary("xor", syncedRead,
                     constant(pointerType, uint64_t(3) << (pointerWidth - 2))));
      auto empty = equal(rg, syncedWrite);
      auto writeAvailable = binary(
          "and", binary("and", writeReleased, peerReadReleased), invert(full));
      auto readAvailable = binary(
          "and", binary("and", readReleased, peerWriteReleased), invert(empty));
      AsyncFIFOUpdateOp update;
      if (fifo)
        for (Operation *user : fifo.getHead().getUsers()) {
          auto candidate = dyn_cast<AsyncFIFOUpdateOp>(user);
          if (candidate && candidate.getHead() == fifo.getHead())
            update = candidate;
        }
      if (fifo && !update)
        return source->emitOpError("missing FIFO update");
      Value pushPayload =
          fifo ? update.getPushPayload() : source->getOperand(3);
      Value pushIntent = fifo ? update.getPushIntent() : source->getOperand(4);
      Value popIntent = fifo ? update.getPopIntent() : source->getOperand(5);
      Type payloadType = pushPayload.getType();
      auto asserted =
          source->getAttrOfType<StringAttr>("reset_polarity").getValue() ==
                  "high"
              ? oneBit
              : zeroBit;
      auto resetInactive = invert(equal(reset, asserted, "case_eq"));
      auto push = binary("and", binary("and", Ref(pushIntent), writeAvailable),
                         resetInactive);
      auto pop = binary("and", binary("and", Ref(popIntent), readAvailable),
                        resetInactive);
      auto nextWrite = binary("add", wb, pointerOne),
           nextRead = binary("add", rb, pointerOne);
      drive(wb, nextWrite, push);
      drive(wg, binary("xor", nextWrite, binary("lshr", nextWrite, pointerOne)),
            push);
      drive(rb, nextRead, pop);
      drive(rg, binary("xor", nextRead, binary("lshr", nextRead, pointerOne)),
            pop);
      auto writeAddress = slice(wb, addressWidth),
           readAddress = slice(rb, addressWidth);
      auto payloadZero = zero(payloadType);
      auto payloadInitial = payloadZero;
      Ref head = payloadZero;
      for (uint64_t slot = 0; slot < depth; ++slot) {
        if (!valid)
          return failure();
        auto address = constant(bits(addressWidth), slot);
        auto bank =
            storage(("payload." + Twine(slot)).str(), payloadType, writeClock,
                    reset, writeEdge, payloadInitial, false);
        drive(bank, Ref(pushPayload),
              binary("and", push, equal(writeAddress, address)));
        head = merge(binary("and", readAvailable, equal(readAddress, address)),
                     bank, head);
      }
      circuit.outputs = {head, writeAvailable, readAvailable};
    }
    if (!valid)
      return failure();
    return std::move(circuit);
  }
};
Operation *registerImport(Operation *resource) {
  Builder b(resource->getContext());
  Type bit = resource->getOperand(0).getType();
  auto payload = TypeParamType::get(
      b.getContext(), FlatSymbolRefAttr::get(b.getContext(), registerSymbol),
      b.getStringAttr("T"));
  SmallVector<Type> inputs{bit, bit, bit, payload, payload, payload};
  SmallVector<Attribute> names;
  for (auto name : {"clk", "rst", "en", "d", "init", "reset_value"})
    names.push_back(b.getStringAttr(name));
  OperationState state(resource->getLoc(), ModuleImportOp::getOperationName());
  state.addAttributes(
      {b.getNamedAttr("sym_name", b.getStringAttr(registerSymbol)),
       b.getNamedAttr(
           "source_owner",
           b.getDictionaryAttr(
               {b.getNamedAttr("package", b.getStringAttr("pycircuit")),
                b.getNamedAttr("path", b.getStringAttr("__builtins__.py"))})),
       b.getNamedAttr("parameters", b.getArrayAttr({})),
       b.getNamedAttr("type_parameters",
                      b.getArrayAttr({b.getStringAttr("T")})),
       b.getNamedAttr("function_type", TypeAttr::get(FunctionType::get(
                                           b.getContext(), inputs, {payload}))),
       b.getNamedAttr("input_names", b.getArrayAttr(names)),
       b.getNamedAttr("output_names", b.getArrayAttr({b.getStringAttr("q")})),
       b.getNamedAttr("semantic_kind", b.getStringAttr("hardware")),
       b.getNamedAttr("primitive_kind", b.getStringAttr("dffe")),
       b.getNamedAttr("ac.register_contract", b.getI64IntegerAttr(3)),
       b.getNamedAttr(
           "dependency_summary",
           b.getArrayAttr({b.getDictionaryAttr(
               {b.getNamedAttr(
                    "output",
                    b.getDictionaryAttr(
                        {b.getNamedAttr("port", b.getI64IntegerAttr(0)),
                         b.getNamedAttr("path", b.getArrayAttr({}))})),
                b.getNamedAttr("inputs", b.getArrayAttr({}))})}))});
  return Operation::create(state);
}
Value resolve(Ref ref, const SmallVector<Operation *, 0> &nodes,
              const IRMapping &mapping) {
  return ref.external ? mapping.lookup(ref.external)
                      : nodes[ref.node]->getResult(ref.result);
}
Operation *firstResource(mlir::ModuleOp package) {
  Operation *first = nullptr;
  package.walk([&](Operation *op) {
    if (!first && isa<AsyncRegOp, AsyncFIFOOp>(op))
      first = op;
  });
  return first;
}
} // namespace
FailureOr<OwningOpRef<mlir::ModuleOp>>
materializeHardwareCDC(mlir::ModuleOp logical,
                       HardwareSourceCheckLimits limits) {
  if (failed(verifyRoutedPackage(logical, limits)))
    return failure();
  detail::SourceCheckBudget budget(limits);
  budget.setPhase("CDC physicalization");
  SmallVector<Circuit, 0> circuits;
  LogicalResult valid = success();
  logical.walk([&](Operation *op) {
    if (!isa<AsyncRegOp, AsyncFIFOOp>(op) || failed(valid))
      return;
    auto spec = Equations(op, budget).run();
    if (failed(spec))
      valid = failure();
    else
      circuits.push_back(std::move(*spec));
  });
  if (failed(valid))
    return failure();
  auto cloneBudget = logical.walk([&](Operation *op) {
    return failed(budget.debit(64 + 16 * op->getNumOperands() +
                                   16 * op->getNumResults() +
                                   32 * op->getAttrs().size(),
                               op))
               ? WalkResult::interrupt()
               : WalkResult::advance();
  });
  if (cloneBudget.wasInterrupted())
    return failure();
  IRMapping mapping;
  OwningOpRef<mlir::ModuleOp> result =
      cast<mlir::ModuleOp>(logical->clone(mapping));
  if (circuits.empty())
    return std::move(result);
  if (SymbolTable::lookupSymbolIn(logical, registerSymbol))
    return logical.emitError(
        "CDC register symbol collides with logical declaration");
  OpBuilder b(logical.getContext());
  b.setInsertionPointToStart(result->getBody());
  b.insert(registerImport(circuits.front().source));
  // Declare all nodes and logical-result replacements before binding any input.
  // SSA graph regions allow feedback through committed storage cuts.
  SmallVector<SmallVector<Operation *, 0>, 0> realized;
  for (auto &circuit : circuits) {
    b.setInsertionPoint(mapping.lookup(circuit.source));
    SmallVector<Operation *, 0> nodes;
    for (auto &node : circuit.nodes) {
      OperationState state(circuit.source->getLoc(), node.operation);
      state.addTypes(node.types);
      state.addAttributes(node.attributes);
      nodes.push_back(b.create(state));
    }
    for (auto [original, ref] :
         llvm::zip(circuit.source->getResults(), circuit.outputs)) {
      auto replacement = resolve(ref, nodes, mapping);
      mapping.lookup(original).replaceAllUsesWith(replacement);
      mapping.erase(original);
      mapping.map(original, replacement);
    }
    realized.push_back(std::move(nodes));
  }
  for (auto [circuit, nodes] : llvm::zip(circuits, realized)) {
    for (auto [equation, operation] : llvm::zip(circuit.nodes, nodes)) {
      SmallVector<Value> inputs;
      for (auto ref : equation.inputs)
        inputs.push_back(resolve(ref, nodes, mapping));
      operation->setOperands(inputs);
    }
    if (auto fifo = dyn_cast<AsyncFIFOOp>(circuit.source)) {
      for (auto *user : fifo.getHead().getUsers())
        if (auto update = dyn_cast<AsyncFIFOUpdateOp>(user);
            update && update.getHead() == fifo.getHead()) {
          mapping.lookup(update.getOperation())->erase();
          break;
        }
    }
    auto *declaration = mapping.lookup(circuit.source);
    OpBuilder witnessBuilder(declaration);
    SmallVector<Value> witnessInputs;
    for (auto input : circuit.source->getOperands())
      witnessInputs.push_back(mapping.lookup(input));
    if (auto fifo = dyn_cast<AsyncFIFOOp>(circuit.source)) {
      for (auto *user : fifo.getHead().getUsers())
        if (auto update = dyn_cast<AsyncFIFOUpdateOp>(user);
            update && update.getHead() == fifo.getHead()) {
          for (Value input : SmallVector<Value>{update.getPushPayload(),
                                                update.getPushIntent(),
                                                update.getPopIntent()})
            witnessInputs.push_back(mapping.lookup(input));
          break;
        }
    }
    for (auto output : circuit.outputs)
      witnessInputs.push_back(resolve(output, nodes, mapping));
    OperationState witness(circuit.source->getLoc(), "ac.cdc.refinement");
    witness.addOperands(witnessInputs);
    witness.addAttributes(circuit.source->getAttrs());
    witness.addAttribute(
        "logical_kind",
        witnessBuilder.getStringAttr(
            isa<AsyncRegOp>(circuit.source) ? "async_reg" : "async_fifo"));
    witness.addAttribute("implementation", witnessBuilder.getI64IntegerAttr(1));
    witnessBuilder.create(witness);
    declaration->erase();
  }
  return std::move(result);
}
namespace {
class Correspondence {
  mlir::ModuleOp logical, expanded;
  detail::SourceCheckBudget budget;
  IRMapping mapping;
  SmallVector<std::pair<Operation *, Operation *>, 0> unchanged;
  struct Expansion {
    Circuit circuit;
    SmallVector<Operation *, 0> nodes;
    Operation *witness = nullptr;
  };
  SmallVector<Expansion, 0> expansions;
  LogicalResult reject(StringRef reason) {
    return logical.emitError() << "CDC correspondence: " << reason;
  }
  LogicalResult index(Operation *source, Operation *target) {
    if (failed(budget.debit(32, source)))
      return failure();
    if (source->getNumResults() != target->getNumResults() ||
        source->getNumRegions() != target->getNumRegions())
      return reject("surrounding operation shape differs");
    unchanged.push_back({source, target});
    mapping.map(source, target);
    for (auto [a, b] : llvm::zip(source->getResults(), target->getResults()))
      mapping.map(a, b);
    for (auto [left, right] :
         llvm::zip(source->getRegions(), target->getRegions())) {
      if (left.getBlocks().size() != right.getBlocks().size())
        return reject("surrounding blocks differ");
      for (auto [a, b] : llvm::zip(left, right)) {
        if (a.getNumArguments() != b.getNumArguments())
          return reject("block arguments differ");
        mapping.map(&a, &b);
        for (auto [x, y] : llvm::zip(a.getArguments(), b.getArguments())) {
          if (x.getType() != y.getType() || x.getLoc() != y.getLoc())
            return reject("argument type/location differs");
          mapping.map(x, y);
        }
        auto cursor = b.begin();
        if (source == logical.getOperation()) {
          if (auto *resource = firstResource(logical)) {
            if (cursor == b.end())
              return reject("missing CDC primitive import");
            Operation *actual = &*cursor++;
            Operation *expected = registerImport(resource);
            bool same =
                actual->getName() == expected->getName() &&
                actual->getAttrDictionary() == expected->getAttrDictionary() &&
                actual->getLoc() == expected->getLoc() &&
                !actual->getNumOperands() && !actual->getNumResults() &&
                !actual->getNumRegions();
            expected->destroy();
            if (!same)
              return reject("CDC primitive import differs");
          }
        }
        for (auto &op : a) {
          if (isa<AsyncFIFOUpdateOp>(op))
            continue;
          if (isa<AsyncRegOp, AsyncFIFOOp>(op)) {
            auto equations = Equations(&op, budget).run();
            if (failed(equations))
              return failure();
            Expansion expansion{std::move(*equations), {}};
            for (auto &node : expansion.circuit.nodes) {
              if (cursor == b.end())
                return reject("missing generated node");
              auto *actual = &*cursor++;
              if (failed(budget.debit(64 + actual->getNumOperands(), &op)))
                return failure();
              if (actual->getName().getStringRef() != node.operation ||
                  actual->getAttrDictionary() !=
                      node.attributes.getDictionary(op.getContext()) ||
                  !llvm::equal(actual->getResultTypes(), node.types) ||
                  actual->getLoc() != op.getLoc() || actual->getNumRegions() ||
                  actual->getNumSuccessors())
                return reject(
                    "generated component kind/type/policy/identity differs");
              expansion.nodes.push_back(actual);
            }
            for (auto [result, ref] :
                 llvm::zip(op.getResults(), expansion.circuit.outputs))
              mapping.map(result, resolve(ref, expansion.nodes, mapping));
            if (cursor == b.end() ||
                cursor->getName().getStringRef() != "ac.cdc.refinement")
              return reject("missing component proof witness");
            expansion.witness = &*cursor++;
            expansions.push_back(std::move(expansion));
          } else {
            if (cursor == b.end())
              return reject("missing surrounding operation");
            if (failed(index(&op, &*cursor++)))
              return failure();
          }
        }
        if (cursor != b.end())
          return reject("extra generated or surrounding operation");
      }
    }
    return success();
  }

public:
  Correspondence(mlir::ModuleOp logical, mlir::ModuleOp expanded,
                 HardwareSourceCheckLimits limits)
      : logical(logical), expanded(expanded), budget(limits) {
    budget.setPhase("CDC independent correspondence");
  }
  LogicalResult run() {
    if (failed(index(logical, expanded)))
      return failure();
    for (auto [source, target] : unchanged) {
      if (failed(budget.debit(32 + source->getNumOperands(), source)))
        return failure();
      if (source->getName() != target->getName() ||
          source->getAttrDictionary() != target->getAttrDictionary() ||
          source->getLoc() != target->getLoc() ||
          !llvm::equal(source->getResultTypes(), target->getResultTypes()) ||
          source->getNumOperands() != target->getNumOperands() ||
          source->getNumSuccessors() != target->getNumSuccessors())
        return reject("surrounding operation semantics differ");
      for (auto [a, b] :
           llvm::zip(source->getOperands(), target->getOperands()))
        if (mapping.lookupOrNull(a) != b)
          return reject("surrounding SSA connection differs");
      for (auto [a, b] :
           llvm::zip(source->getSuccessors(), target->getSuccessors()))
        if (mapping.lookupOrNull(a) != b)
          return reject("surrounding successor differs");
    }
    // Complete the surrounding SSA map before checking resource inputs: a FIFO
    // update or another resource may refer forward to a later graph operation.
    // Final-only legality cannot prove that the witness retained the source
    // endpoint identities, including controls unused by the selected circuit.
    for (auto &expansion : expansions) {
      auto *source = expansion.circuit.source;
      auto *witness = expansion.witness;
      if (failed(budget.debit(64 + source->getNumOperands() +
                                     source->getNumResults(),
                             source)))
        return failure();
      Builder b(source->getContext());
      NamedAttrList attributes(source->getAttrs());
      attributes.set(
          "logical_kind",
          b.getStringAttr(isa<AsyncRegOp>(source) ? "async_reg" : "async_fifo"));
      attributes.set("implementation", b.getI64IntegerAttr(1));
      if (witness->getAttrDictionary() !=
              attributes.getDictionary(source->getContext()) ||
          witness->getLoc() != source->getLoc() || witness->getNumResults() ||
          witness->getNumRegions() || witness->getNumSuccessors())
        return reject("resource witness policy/identity differs");
      SmallVector<Value> inputs(source->getOperands());
      if (auto fifo = dyn_cast<AsyncFIFOOp>(source)) {
        AsyncFIFOUpdateOp update;
        for (auto *user : fifo.getHead().getUsers())
          if (auto candidate = dyn_cast<AsyncFIFOUpdateOp>(user);
              candidate && candidate.getHead() == fifo.getHead())
            update = candidate;
        if (!update)
          return reject("missing logical FIFO update");
        inputs.push_back(update.getPushPayload());
        inputs.push_back(update.getPushIntent());
        inputs.push_back(update.getPopIntent());
      }
      llvm::append_range(inputs, source->getResults());
      if (inputs.size() != witness->getNumOperands())
        return reject("resource witness pin inventory differs");
      for (auto [original, actual] :
           llvm::zip(inputs, witness->getOperands()))
        if (mapping.lookupOrNull(original) != actual)
          return reject("resource witness SSA connection differs");
      if (failed(verifyFinalHardwareCDCComponent(witness)))
        return failure();
      for (auto [equation, actual] :
           llvm::zip(expansion.circuit.nodes, expansion.nodes)) {
        if (equation.inputs.size() != actual->getNumOperands())
          return reject("component pin inventory differs");
        for (auto [ref, input] :
             llvm::zip(equation.inputs, actual->getOperands()))
          if (resolve(ref, expansion.nodes, mapping) != input)
            return reject(
                "component equation/clock/reset/enable connection differs");
      }
    }
    return success();
  }
};
} // namespace
namespace {
LogicalResult verifyFinalHardwareCDCComponent(Operation *witness) {
  auto reject = [&]() {
    return witness->emitOpError("invalid CDC component refinement witness");
  };
  if (failed(verifyCDCRefinementContract(witness)))
    return failure();
  auto package = witness->getParentOfType<mlir::ModuleOp>();
  if (failed(verifyCDCRefinementIntents(
          HardwareAnalysis(package), cast<CDCRefinementOp>(witness), {})))
    return failure();
  auto imported = dyn_cast_or_null<ModuleImportOp>(
      SymbolTable::lookupSymbolIn(package, registerSymbol));
  auto version =
      imported ? getRegisterContract(imported) : FailureOr<unsigned>(failure());
  if (!imported || failed(version) || *version != 3 ||
      imported->getAttrOfType<StringAttr>("primitive_kind").getValue() !=
          "dffe" ||
      failed(HardwareAnalysis(package).verifyImport(imported)))
    return reject();
  auto kind = witness->getAttrOfType<StringAttr>("logical_kind");
  auto implementation = witness->getAttrOfType<IntegerAttr>("implementation");
  bool reg = kind && kind.getValue() == "async_reg";
  if (!kind || (!reg && kind.getValue() != "async_fifo") || !implementation ||
      implementation.getInt() != 1 ||
      witness->getNumOperands() != (reg ? 5u : 9u) ||
      witness->getNumResults() || witness->getNumRegions())
    return reject();
  detail::SourceCheckBudget budget(HardwareSourceCheckLimits{});
  auto equations = Equations(witness, budget).run();
  if (failed(equations))
    return failure();
  SmallVector<Operation *, 0> nodes;
  auto cursor = witness->getIterator();
  for (size_t i = 0; i < equations->nodes.size(); ++i) {
    if (cursor == witness->getBlock()->begin())
      return reject();
    nodes.push_back(&*--cursor);
  }
  std::reverse(nodes.begin(), nodes.end());
  IRMapping identity;
  for (auto operand : witness->getOperands())
    identity.map(operand, operand);
  llvm::DenseSet<Operation *> owned(nodes.begin(), nodes.end());
  for (auto [equation, actual] : llvm::zip(equations->nodes, nodes))
    if (!llvm::equal(actual->getResultTypes(), equation.types))
      return reject();
  llvm::DenseSet<Value> exposed;
  unsigned outputBase = reg ? 4 : 6;
  for (auto [i, ref] : llvm::enumerate(equations->outputs)) {
    auto output = resolve(ref, nodes, identity);
    if (output != witness->getOperand(outputBase + i))
      return reject();
    exposed.insert(output);
  }
  for (auto [equation, actual] : llvm::zip(equations->nodes, nodes)) {
    if (actual->getName().getStringRef() != equation.operation ||
        actual->getAttrDictionary() !=
            equation.attributes.getDictionary(witness->getContext()) ||
        !llvm::equal(actual->getResultTypes(), equation.types) ||
        actual->getNumOperands() != equation.inputs.size() ||
        actual->getNumRegions() || actual->getNumSuccessors())
      return reject();
    for (auto [ref, input] : llvm::zip(equation.inputs, actual->getOperands()))
      if (resolve(ref, nodes, identity) != input)
        return reject();
    for (auto result : actual->getResults())
      for (auto *user : result.getUsers())
        if (user != witness && !owned.contains(user) &&
            !exposed.contains(result))
          return reject();
  }
  return success();
}
} // namespace
LogicalResult verifyHardwareCDCComponent(Operation *witness) {
  return verifyFinalHardwareCDCComponent(witness);
}
FailureOr<DenseMap<Operation *, Operation *>>
getVerifiedCDCComponentOwners(mlir::ModuleOp package) {
  LogicalResult valid = success();
  DenseMap<Operation *, Operation *> covered;
  package.walk([&](Operation *op) {
    if (op->getName().getStringRef() != "ac.cdc.refinement" || failed(valid))
      return;
    if (failed(verifyFinalHardwareCDCComponent(op))) {
      valid = failure();
      return;
    }
    detail::SourceCheckBudget budget(HardwareSourceCheckLimits{});
    auto equations = Equations(op, budget).run();
    if (failed(equations)) {
      valid = failure();
      return;
    }
    auto cursor = op->getIterator();
    for (size_t i = 0; i < equations->nodes.size(); ++i) {
      auto *node = &*--cursor;
      if (!covered.try_emplace(node, op).second) {
        op->emitOpError("overlapping CDC component ownership");
        valid = failure();
      }
    }
  });
  package.walk([&](Operation *op) {
    if (op->hasAttr("ac.cdc_component") && !covered.count(op)) {
      op->emitOpError(
          "CDC component lacks a verified complete refinement witness");
      valid = failure();
    }
  });
  if (failed(valid))
    return failure();
  return covered;
}
LogicalResult verifyFinalHardwareCDCComponents(mlir::ModuleOp package) {
  return success(succeeded(getVerifiedCDCComponentOwners(package)));
}
LogicalResult
verifyHardwareCDCMaterialization(mlir::ModuleOp logical,
                                 mlir::ModuleOp expanded,
                                 HardwareSourceCheckLimits limits) {
  if (!expanded || failed(verifyRoutedPackage(logical, limits)))
    return failure();
  return Correspondence(logical, expanded, limits).run();
}
} // namespace acir::ac

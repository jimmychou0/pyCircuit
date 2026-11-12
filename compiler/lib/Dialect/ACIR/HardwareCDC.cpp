#include "pycircuit/Dialect/ACIR/HardwareCDC.h"
#include "HardwareCDCLocalVerification.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
using namespace mlir;
namespace acir::ac {
namespace {
LogicalResult verifyFIFOIntents(const HardwareAnalysis &analysis, Operation *resource, ArrayRef<Value> intents, const HardwareBindings &bindings) {
  for (auto intent : intents) {
    if (auto constant = intent.getDefiningOp<BitsConstantOp>()) {
      auto value = analysis.evaluateStatic(constant.getValue(), bindings, constant);
      if (succeeded(value))
        if (auto number = dyn_cast<MathIntAttr>(*value);
            number && number.getCanonicalValue() == "0")
          continue;
    }
    auto rule = intent.getDefiningOp<RuleOp>();
    if (!rule)
      return resource->emitOpError(
          "nonzero endpoint intent requires a sampled transaction fire");
    auto effects = getAsyncFIFOEndpointEffects(rule);
    if (failed(effects))
      return failure();
    if (llvm::none_of(*effects, [&](auto &effect) {
          return effect.resource == resource && effect.intent == intent;
        }))
      return resource->emitOpError(
          "endpoint intent lacks authenticated admission");
  }
  return success();
}
} // namespace

LogicalResult verifyCDCRefinementIntents(
    const HardwareAnalysis &analysis, CDCRefinementOp witness,
    const HardwareBindings &bindings) {
  if (failed(verifyCDCRefinementContract(witness)))
    return failure();
  if (witness.getLogicalKind() != "async_fifo")
    return success();
  return verifyFIFOIntents(analysis, witness,
                          {witness.getOperand(4), witness.getOperand(5)},
                          bindings);
}

bool hasLogicalCDC(Operation *root) {
  bool found = false;
  root->walk([&](Operation *op) {
    found |= isa<AsyncRegOp, AsyncFIFOOp, AsyncFIFOUpdateOp>(op);
  });
  return found;
}
LogicalResult verifyBoundHardwareCDCOperation(
    const HardwareAnalysis &analysis, Operation *op,
    const HardwareBindings &bindings) {
  SmallVector<Value> controls;
  if (auto reg = dyn_cast<AsyncRegOp>(op)) {
    controls.append(op->getOperands().begin(), op->getOperands().begin() + 4);
    controls.push_back(reg.getOutput());
  } else if (auto fifo = dyn_cast<AsyncFIFOOp>(op)) {
    controls.append(op->getOperands().begin(), op->getOperands().begin() + 3);
    controls.append({fifo.getWriteAvailable(), fifo.getReadAvailable()});
  } else if (auto update = dyn_cast<AsyncFIFOUpdateOp>(op)) {
    controls.append({update.getPushIntent(), update.getPopIntent()});
    if (failed(verifyFIFOIntents(analysis, update.getHead().getDefiningOp(),
                                 {update.getPushIntent(), update.getPopIntent()}, bindings)))
      return failure();
  } else if (auto witness = dyn_cast<CDCRefinementOp>(op)) {
    if (failed(verifyCDCRefinementContract(op))) return failure();
    bool reg = witness.getLogicalKind() == "async_reg";
    for (unsigned ordinal : reg ? SmallVector<unsigned>{0, 1, 2, 3, 4}
                                : SmallVector<unsigned>{0, 1, 2, 4, 5, 7, 8})
      controls.push_back(witness.getOperand(ordinal));
    if (failed(verifyCDCRefinementIntents(analysis, witness, bindings)))
      return failure();
    if (failed(verifyHardwareCDCComponent(op))) return failure();
  }
  for (Value control : controls) {
    auto width = analysis.getPackedWidth(control.getType(), bindings, op);
    if (!isa<BitsType>(control.getType()) || failed(width) || *width != 1)
      return op->emitOpError("CDC controls must be one bit");
  }
  return success();
}
} // namespace acir::ac
namespace acir::ac {
namespace {
bool containsAdmission(Value value, Value admission, RuleOp rule,
                       DenseSet<Value> &visited, unsigned depth = 0) {
  if (depth > 128 || !visited.insert(value).second)
    return false;
  if (value == admission)
    return true;
  if (auto argument = dyn_cast<BlockArgument>(value)) {
    if (argument.getOwner()->getParentOp() == rule)
      return containsAdmission(rule.getCaptures()[argument.getArgNumber()],
                               admission, rule, visited, depth + 1);
  }
  if (auto op = value.getDefiningOp<BitsBinaryOp>();
      op && op.getOpcode() == "and")
    return containsAdmission(op.getLhs(), admission, rule, visited,
                             depth + 1) ||
           containsAdmission(op.getRhs(), admission, rule, visited, depth + 1);
  return false;
}
} // namespace
FailureOr<SmallVector<HardwareCDCEndpointEffect>>
getAsyncFIFOEndpointEffects(RuleOp rule) {
  SmallVector<HardwareCDCEndpointEffect> effects;
  auto row = rule->getAttrOfType<DictionaryAttr>("ac.sampled_admission");
  auto fire = row ? row.getAs<IntegerAttr>("fire_result") : IntegerAttr();
  auto clockCapture =
      row ? row.getAs<IntegerAttr>("clock_capture") : IntegerAttr();
  for (auto result : rule.getResults())
    for (auto &use : result.getUses()) {
      auto update = dyn_cast<AsyncFIFOUpdateOp>(use.getOwner());
      auto witness = dyn_cast<CDCRefinementOp>(use.getOwner());
      bool logical = update && (use.getOperandNumber() == 2 ||
                                use.getOperandNumber() == 3);
      bool physical =
          witness && witness.getLogicalKind() == "async_fifo" &&
          (use.getOperandNumber() == 4 || use.getOperandNumber() == 5);
      if (!logical && !physical)
        continue;
      bool write = use.getOperandNumber() == (logical ? 2u : 4u);
      Operation *resource =
          logical ? update.getHead().getDefiningOp() : witness.getOperation();
      if (!resource || !isa<AsyncFIFOOp, CDCRefinementOp>(resource) ||
          resource->getNumOperands() < (logical ? 3u : 9u))
        return rule.emitOpError("invalid CDC resource declaration");
      auto fifo = dyn_cast<AsyncFIFOOp>(resource);
      if (!fire || fire.getInt() < 0 ||
          uint64_t(fire.getInt()) >= rule.getNumResults() ||
          result != rule.getResult(fire.getInt()) || !clockCapture ||
          clockCapture.getInt() < 0 ||
          uint64_t(clockCapture.getInt()) >= rule.getCaptures().size())
        return rule.emitOpError("CDC transfer intent must be its endpoint "
                                "transaction's authenticated final fire");
      auto expectedClock = resource->getOperand(write ? 0 : 1);
      auto edge = resource->getAttrOfType<StringAttr>(write ? "write_edge"
                                                            : "read_edge");
      auto polarity = resource->getAttrOfType<StringAttr>("reset_polarity");
      if (!edge || !polarity ||
          resource->getNumOperands() < (logical ? 3u : 9u))
        return rule.emitOpError("invalid CDC endpoint contract");
      // Actual clock identity is proved for every instantiated writer by the
      // common effect-footprint proof; distinct formal pins may alias there.
      if (row.getAs<StringAttr>("edge") != edge)
        return rule.emitOpError("CDC endpoint transaction clock/edge differs");
      bool resetPresent = false;
      if (auto resets = row.getAs<ArrayAttr>("resets"))
        for (auto raw : resets) {
          auto reset = dyn_cast<DictionaryAttr>(raw);
          auto capture =
              reset ? reset.getAs<IntegerAttr>("capture") : IntegerAttr();
          if (capture && capture.getInt() >= 0 &&
              uint64_t(capture.getInt()) < rule.getCaptures().size() &&
              rule.getCaptures()[capture.getInt()] == resource->getOperand(2) &&
              reset.getAs<StringAttr>("kind") ==
                  StringAttr::get(rule.getContext(), "async") &&
              reset.getAs<StringAttr>("polarity") == polarity)
            resetPresent = true;
        }
      if (!resetPresent)
        return rule.emitOpError("CDC endpoint transaction must include its "
                                "actual whole asynchronous reset");
      auto yielded = cast<YieldOp>(rule.getBody().front().back());
      auto available =
          logical ? (write ? fifo.getWriteAvailable() : fifo.getReadAvailable())
                  : resource->getOperand(write ? 7 : 8);
      DenseSet<Value> admissionVisits;
      if (!containsAdmission(yielded.getOperand(fire.getInt()), available, rule,
                             admissionVisits))
        return rule.emitOpError(
            "CDC endpoint fire must include its actual local availability");
      effects.push_back(
          {resource, expectedClock, resource->getOperand(2), result, available,
           edge.getValue() == "posedge" ? RegisterEdge::Posedge
                                        : RegisterEdge::Negedge,
           polarity.getValue() == "high" ? RegisterResetPolarity::High
                                         : RegisterResetPolarity::Low,
           write});
    }
  return effects;
}
} // namespace acir::ac

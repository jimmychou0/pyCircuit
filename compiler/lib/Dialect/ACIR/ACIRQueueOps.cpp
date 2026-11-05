#include "ACIRSourceContracts.h"
#include "pycircuit/Dialect/ACIR/HardwareAnalysis.h"
#include "pycircuit/Interfaces/LogicalStateInterfaces.h"
#include "pycircuit/Dialect/ACIR/SourceMethodAnalysis.h"
#include "pycircuit/Dialect/ACIR/SourceIdentifier.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"

#include <functional>
#include <limits>
#include <optional>

using namespace mlir;
namespace acir::ac {

bool isQueueInstance(InstanceOp instance) {
  if (!instance)
    return false;
  auto callee = instance.getCalleeAttr();
  if (!callee)
    return false;
  return callee.getValue() == "pycircuit.__builtins__.fifo_local" ||
         callee.getValue() == "pycircuit.__builtins__.fifo_downstream_pop";
}

FailureOr<DictionaryAttr> getQueueConfiguration(InstanceOp instance) {
  if (!isQueueInstance(instance))
    return instance.emitOpError()
           << "queue configuration requires a canonical FIFO instance";
  return getQueueConfiguration(instance.getCalleeAttr(),
                               instance.getParameters(), instance);
}

FailureOr<DictionaryAttr>
getQueueConfiguration(FlatSymbolRefAttr callee, ArrayAttr parameters,
                      Operation *diagnosticSite) {
  if (!diagnosticSite)
    return failure();
  if (!callee ||
      (callee.getValue() != "pycircuit.__builtins__.fifo_local" &&
       callee.getValue() !=
           "pycircuit.__builtins__.fifo_downstream_pop"))
    return diagnosticSite->emitOpError()
           << "queue configuration requires a canonical FIFO callee";
  if (!parameters || parameters.size() != 2)
    return diagnosticSite->emitOpError()
           << "canonical FIFO instance requires explicit DEPTH and "
              "AVAILABILITY_LATENCY arguments";
  auto depth = dyn_cast<StaticExprAttr>(parameters[0]);
  auto latency = dyn_cast<StaticExprAttr>(parameters[1]);
  if (!depth || !latency)
    return diagnosticSite->emitOpError()
           << "canonical FIFO arguments require StaticExpr values";
  Builder b(diagnosticSite->getContext());
  auto origin = depth.getTree().getAs<DictionaryAttr>("origin");
  auto location = depth.getTree().getAs<DictionaryAttr>("location");
  if (!origin || !location)
    return diagnosticSite->emitOpError()
           << "canonical FIFO DEPTH requires source provenance";
  auto zeroTree = b.getDictionaryAttr({
      b.getNamedAttr("kind", b.getStringAttr("literal")),
      b.getNamedAttr("origin", origin), b.getNamedAttr("location", location),
      b.getNamedAttr(
          "value", b.getDictionaryAttr({
                       b.getNamedAttr("kind", b.getStringAttr("integer")),
                       b.getNamedAttr(
                           "value", MathIntAttr::get(
                                        diagnosticSite->getContext(),
                                        llvm::APSInt::getUnsigned(0)))}))});
  auto headRead =
      StaticExprAttr::get(diagnosticSite->getContext(), zeroTree);
  StringRef readyPolicy = callee.getValue() ==
                                  "pycircuit.__builtins__.fifo_local"
                              ? "local_occupancy"
                              : "downstream_pop";
  return b.getDictionaryAttr({
      b.getNamedAttr("depth", depth),
      b.getNamedAttr("ready_policy", b.getStringAttr(readyPolicy)),
      b.getNamedAttr("availability_latency", latency),
      b.getNamedAttr("head_read_latency", headRead),
      b.getNamedAttr("empty_flow", b.getBoolAttr(false)),
      b.getNamedAttr("read_during_write", b.getStringAttr("old")),
      b.getNamedAttr("reset_policy", b.getStringAttr("sync_high_empty")),
      b.getNamedAttr("empty_data", b.getStringAttr("zero"))});
}


LogicalResult SourceQueuePopOp::verify() {
  return emitOpError()
         << "retired dedicated queue effects are not admitted; use a native "
            "FIFO Method call";
}
LogicalResult SourceQueuePeekOp::verify() {
  return emitOpError()
         << "retired dedicated queue effects are not admitted; use a native "
            "FIFO Method call";
}
LogicalResult SourceQueuePushOp::verify() {
  return emitOpError()
         << "retired dedicated queue effects are not admitted; use a native "
            "FIFO Method call";
}

LogicalResult QueueOp::verify() {
  return emitOpError()
         << "retired ac.queue is not admitted; use a canonical FIFO instance";
}

} // namespace acir::ac
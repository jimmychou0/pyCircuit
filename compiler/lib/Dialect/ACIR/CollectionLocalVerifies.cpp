// Collection operation local verifiers (B2 step-2 design r9): these stay
// in the operations layer; the shared member implementations live in
// CollectionAnalysis.cpp on the analysis side.
#include "pycircuit/Dialect/ACIR/HardwareAnalysis.h"
#include "ACIRSourceContracts.h"
#include "pycircuit/Dialect/ACIR/ACIROps.h"

using namespace mlir;

namespace acir::ac {

LogicalResult CollectionOp::verify() {
  auto parent = dyn_cast_or_null<ModuleOp>((*this)->getParentOp());
  if (!parent || getInstanceName().empty())
    return emitOpError() << "collection requires named direct module placement";
  for (Operation &other : parent.getBody().front())
    if (&other != getOperation() && isa<InstanceOp, CollectionOp, QueueOp>(other) &&
        other.getAttrOfType<StringAttr>("instance_name") ==
            getInstanceNameAttr())
      return emitOpError() << "instance_name must be unique within parent";
  if (failed(detail::verifyOccurrence(getOccurrence(),
                                      [&] { return emitOpError(); })))
    return failure();
  return detail::verifyCollectionLocally(*this);
}
LogicalResult CollectionOp::verifySymbolUses(SymbolTableCollection &) {
  return detail::verifyCollectionLocally(*this);
}
LogicalResult TableCreateOp::verify() { return detail::verifyCollectionLocally(*this); }
LogicalResult TableSplatOp::verify() { return detail::verifyCollectionLocally(*this); }
LogicalResult TableMapOp::verify() { return detail::verifyCollectionLocally(*this); }
LogicalResult TableViewOp::verify() { return detail::verifyCollectionLocally(*this); }
LogicalResult TableIndexOp::verify() { return detail::verifyCollectionLocally(*this); }
LogicalResult TableReadOp::verify() { return detail::verifyCollectionLocally(*this); }
LogicalResult TableGetOp::verify() { return detail::verifyCollectionLocally(*this); }
LogicalResult TableMatchOp::verify() { return detail::verifyCollectionLocally(*this); }
LogicalResult TableChooseOp::verify() { return detail::verifyCollectionLocally(*this); }
LogicalResult TableFoldOp::verify() { return detail::verifyCollectionLocally(*this); }
LogicalResult ValueMergeOp::verify() { return detail::verifyCollectionLocally(*this); }
LogicalResult TableMapOp::verifyRegions() {
  if (getBody().empty() || getBody().front().getArguments().empty())
    return emitOpError() << "table.map requires ordinal argument";
  SmallVector<Type> args{getBody().front().getArgument(0).getType()}, results;
  for (Value v : getTables())
    args.push_back(cast<TableType>(v.getType()).getElementType());
  llvm::append_range(args, getCaptures().getTypes());
  for (Value v : getResults())
    results.push_back(cast<TableType>(v.getType()).getElementType());
  return scalarRegion(*this, args, results);
}
LogicalResult TableMatchOp::verifyRegions() {
  SmallVector<Type> args{getInput().getType().getElementType()};
  llvm::append_range(args, getCaptures().getTypes());
  if (getBody().empty() || getBody().front().empty() ||
      !isa<YieldOp>(getBody().front().back()) ||
      getBody().front().back().getNumOperands() != 1)
    return emitOpError() << "table.match predicate must yield one bit";
  auto result =
      dyn_cast<BitsType>(getBody().front().back().getOperand(0).getType());
  if (!result)
    return emitOpError() << "table.match predicate must yield one bit";
  HardwareAnalysis a((*this)->getParentOfType<mlir::ModuleOp>());
  auto w = a.getPackedWidth(result, scope(*this), *this);
  if (failed(w) || *w != 1)
    return emitOpError() << "table.match predicate must yield one bit";
  return scalarRegion(*this, args, TypeRange{result});
}
} // namespace acir::ac

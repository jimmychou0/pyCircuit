#ifndef PYCIRCUIT_CORE_PROBE
#include "pycircuit/Dialect/ACIR/ACIRDialect.h"
#include "pycircuit/Dialect/GFIR/GFIRDialect.h"
#else
#include "pycircuit/Dialect/ACIR/ACIRTypes.h"
#endif
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/Dialect.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/OperationSupport.h"

#include "gtest/gtest.h"

#ifdef PYCIRCUIT_CORE_PROBE
// Core probe: whole-archive linking pulls EVERY core member, and the TU
// references one symbol per enumerated core TU, so any reverse dependency
// inside ACIRCore fails here at link time (r9 §1 per-TU symbol table).
#include "pycircuit/Dialect/ACIR/ACIRTypes.h"
#include "pycircuit/Dialect/ACIR/ACIRAttributes.h"
#include "Dialect/ACIR/ACIRSourceContracts.h"
#include "IR/StateDeclarationContracts.h"
#include "mlir/IR/BuiltinAttributes.h"
namespace {
TEST(ACIRCoreStandalone, ReferencesEveryCoreUnit) {
  auto *context = static_cast<mlir::MLIRContext *>(nullptr);
  (void)context;
  // ACIRCoreDefs: generated type accessor symbol.
  (void)&acir::ac::BitsType::get;
  // ACIRPacketTypes: the relocated shape grammar.
  (void)&acir::ac::detail::shapeSchema;
  // ACIRIdentityContracts: occurrence grammar.
  (void)&acir::ac::detail::verifyOccurrence;
  // ACIRStaticContracts + static cluster: template type leaf.
  (void)&acir::ac::detail::verifySourceTemplateType;
  // ACIRSourceContracts: shared contracts entry.
  (void)&acir::ac::areEquivalentHardwareTypes;
  // IR/StateDeclarationContracts: declared-category placement.
  (void)&acir::verifyRoutedLogicalStatePlacement;
  SUCCEED();
}
} // namespace
#else
// Ops-layer containment probe: this executable links ONLY the ACIRDialect
// target. Loading the dialect and resolving registered operations must
// pull no symbol from a target above the operations layer that is not on
// its link line (B2 step-2 design r9 §4); the configure-time
// pycircuit_verify_layering walk asserts the transitive closure.
namespace {
TEST(ACIROpsStandalone, LoadsAndResolvesItsOperations) {
  mlir::DialectRegistry registry;
  registry.insert<acir::ac::ACIRDialect>();
  mlir::MLIRContext context(registry);
  auto *dialect = context.getOrLoadDialect<acir::ac::ACIRDialect>();
  ASSERT_TRUE(dialect != nullptr);
  EXPECT_EQ(dialect->getNamespace(), "ac");
  EXPECT_TRUE(mlir::RegisteredOperationName::lookup("ac.module", &context));
  EXPECT_TRUE(mlir::RegisteredOperationName::lookup("ac.instance", &context));
  EXPECT_TRUE(mlir::RegisteredOperationName::lookup("ac.state.read", &context));
}
} // namespace
#endif

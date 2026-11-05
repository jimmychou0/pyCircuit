#include "pycircuit/Dialect/ACIR/ACIRTypes.h"
#include "IR/StateDeclarationContracts.h"
#include "pycircuit/Dialect/ACIR/ACIRDialect.h"
#include "pycircuit/Dialect/GFIR/GFIRDialect.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/MLIRContext.h"

#include "gtest/gtest.h"

#ifdef PYCIRCUIT_CORE_PROBE
// Core probe: whole-archive linking pulls EVERY core member, and this TU
// references one symbol per enumerated core TU, so any reverse dependency
// inside ACIRCore fails here at link time (r9 §1 per-TU symbol table).
#include "pycircuit/Dialect/ACIR/ACIRDialect.h"
#include "pycircuit/Dialect/GFIR/GFIRDialect.h"
#include "mlir/IR/OperationSupport.h"
namespace {
TEST(ACIRCoreStandalone, ReferencesEveryCoreUnit) {
  // ACIRCoreDefs + PacketTypes: generated type accessor.
  (void)&acir::ac::BitsType::get;
  // StateDeclarationContracts: placement contract.
  (void)&acir::ac::verifyRoutedLogicalStatePlacement;
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

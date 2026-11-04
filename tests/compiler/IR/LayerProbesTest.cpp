#ifndef PYCIRCUIT_CORE_PROBE
#include "pycircuit/Dialect/ACIR/ACIRDialect.h"
#include "pycircuit/Dialect/GFIR/GFIRDialect.h"
#else
// Core probe: the generated core definitions (ACIRCoreDefs) include the
// dialect class declaration; the probe constructs core types without any
// operations-layer symbol. Its guarantee is that the link line carries
// exactly ACIRCore.
#include "pycircuit/Dialect/ACIR/ACIRDialect.h"
#endif
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/Dialect.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/OperationSupport.h"

#include "gtest/gtest.h"

// Ops-layer containment probe: this executable links ONLY the ACIRDialect
// target. Loading the dialect and resolving registered operations must
// pull no symbol from a layer above the operations archive (B2 step-2
// design r9 §4); the build-time verify-layering walk additionally asserts
// the resolved link line carries no analysis/HWIR target.
namespace {

#ifndef PYCIRCUIT_CORE_PROBE
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
#endif

} // namespace

#ifdef PYCIRCUIT_CORE_PROBE
#ifdef PYCIRCUIT_CORE_PROBE
// Core probe body: type construction exercises the generated core
// definitions (ACIRCoreDefs) without any operations-layer symbol.
TEST(ACIRCoreStandalone, ConstructsCoreTypes) {
  mlir::MLIRContext context;
  context.getOrLoadDialect<acir::ac::ACIRDialect>();
  auto bits = acir::ac::BitsType::get(
      &context, acir::ac::StaticExprAttr());
  (void)bits;
  SUCCEED();
}
#endif
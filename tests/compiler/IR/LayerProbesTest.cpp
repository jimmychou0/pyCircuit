#include "pycircuit/Dialect/ACIR/ACIRDialect.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/Dialect.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/OperationSupport.h"

#include "gtest/gtest.h"

#ifdef PYCIRCUIT_CORE_PROBE
// Core probe: constructs generated core types (ACIRCoreDefs) with the
// dialect loaded from the core archive's own registration closure. The
// guarantee is that this executable's link line carries exactly ACIRCore
// (verify-layering enforces the direction); loading the ACIR dialect in
// this build registers attributes/types but no operations, because the
// op registration TU compiles in the operations layer above.
namespace {
TEST(ACIRCoreStandalone, LoadsCoreDialect) {
  mlir::MLIRContext context;
  auto *dialect = context.getOrLoadDialect<acir::ac::ACIRDialect>();
  ASSERT_TRUE(dialect != nullptr);
  EXPECT_EQ(dialect->getNamespace(), "ac");
}
} // namespace
#else
// Ops-layer containment probe: this executable links ONLY the ACIRDialect
// target. Loading the dialect and resolving registered operations must
// pull no symbol from a layer above the operations archive (B2 step-2
// design r9 §4); the configure-time verify-layering walk additionally
// asserts the resolved interface closure carries no analysis/HWIR target.
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

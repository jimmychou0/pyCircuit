#include "pycircuit/Dialect/GFIR/GFIRDialect.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/Dialect.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/OperationSupport.h"

#include "gtest/gtest.h"

// Link-containment probe: this test executable links ONLY the GFIR dialect
// target (plus gtest). It detects UNDECLARED link dependencies — any GFIR
// object whose symbols live in an archive this executable does not link
// fails here at link time. It cannot bound closure GROWTH: an explicitly
// added upper-layer dependency on GFIRDialect would still link. Today
// GFIRDialect transitively links the ACIR monolith by design; step 2 of
// the layering plan narrows that closure, and this probe's expectations
// tighten with it (B1/B2 boundary record).
namespace {

TEST(GFIRStandalone, LoadsAndResolvesItsOperations) {
  mlir::DialectRegistry registry;
  registry.insert<acir::gfir::GFIRDialect>();
  mlir::MLIRContext context(registry);
  auto *dialect = context.getOrLoadDialect<acir::gfir::GFIRDialect>();
  ASSERT_TRUE(dialect != nullptr);
  EXPECT_EQ(dialect->getNamespace(), "gfir");
  EXPECT_TRUE(mlir::RegisteredOperationName::lookup("gfir.state", &context));
  EXPECT_TRUE(
      mlir::RegisteredOperationName::lookup("gfir.state.update", &context));
}

} // namespace

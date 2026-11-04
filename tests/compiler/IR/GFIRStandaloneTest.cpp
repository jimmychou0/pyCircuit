#include "pycircuit/Dialect/GFIR/GFIRDialect.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/Dialect.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/OperationSupport.h"

#include "gtest/gtest.h"

// Link-containment probe: this test executable links ONLY the GFIR dialect
// target (plus gtest). Loading the dialect and resolving its two registered
// operations must pull no symbol from the ACIR operation or staged-analysis
// archives; a regression that reintroduces such a dependency fails here at
// link time before any gate runs (B1/B2 boundary record). Until step 2 of
// the layering plan lands, GFIRDialect still links the ACIR monolith by
// design; the probe fixes the CURRENT closure as the containment floor and
// will be tightened to link nothing else when the split completes.
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

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
// Core probe: the dialect class constructor compiles in the operations
// layer (r9 keeps registration ops-side), so the core archive provides no
// loadable dialect. This executable's guarantee is its LINK LINE: it
// carries exactly ACIRCore (enforced by verify-layering), and linking
// succeeds only if the core archive is self-contained for the symbols
// this TU references — none, by design: the linker would reject any
// undeclared dependency from the archive's extracted members only when
// they are referenced. The per-TU symbol table of r9 §1 lands with the
// analysis slice; today this probe pins the target graph.
namespace {
TEST(ACIRCoreStandalone, CoreTargetLinks) { SUCCEED(); }
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

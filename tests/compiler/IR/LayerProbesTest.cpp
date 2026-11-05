#include "pycircuit/Dialect/ACIR/ACIRDialect.h"
#include "pycircuit/Dialect/ACIR/ACIRTypes.h"
#include "pycircuit/Dialect/GFIR/GFIRDialect.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/MLIRContext.h"

#include "gtest/gtest.h"

#ifdef PYCIRCUIT_CORE_PROBE
// Core probe: the executable links the ACIRCore target as WHOLE_ARCHIVE,
// so every core member object is pulled regardless of use, and any
// reverse dependency inside ACIRCore fails here at link time. This TU
// references one symbol per enumerated core TU so the probe also detects
// a member that silently stopped being part of the closure (r9 §4).
#include "pycircuit/Dialect/ACIR/ACIRAttributes.h"
#include "pycircuit/Dialect/ACIR/ACIRTypes.h"
#include "Dialect/ACIR/ACIRSourceContracts.h"
#include "Dialect/ACIR/ACIRStaticEvaluation.h"
#include "Dialect/ACIR/SourceStaticRecordConstructors.h"
#include "Dialect/ACIR/SourceStaticDomains.h"
#include "Dialect/ACIR/SourceSyntaxSiteContracts.h"
#include "Dialect/ACIR/SourceValueSemantics.h"
#include "pycircuit/Dialect/ACIR/SourceIdentifier.h"
#include "IR/StateDeclarationContracts.h"
namespace {
TEST(ACIRCoreStandalone, ReferencesEveryCoreUnit) {
  // ACIRCoreDefs: generated attribute classes and their registration.
  (void)&acir::ac::MathIntAttr::get;
  // ACIRPacketTypes: packet type hierarchy and its verification.
  (void)&acir::ac::BitsType::get;
  // ACIRIdentityContracts: occurrence grammar and its memo.
  (void)&acir::ac::detail::verifyOccurrence;
  // ACIRSourceContracts: source site/occurrence contract leaves.
  (void)&acir::ac::detail::verifySite;
  // SourceStaticValueContracts: closed static-value grammar.
  (void)&acir::ac::detail::verifyStaticValueStructure;
  // ACIRStaticContracts: static expression tree verification.
  (void)&acir::ac::StaticExprAttr::verify;
  // SourceAttributeContracts: source domain/type-expr attr verifiers.
  (void)&acir::ac::SourceDomainAttr::verify;
  // SourceSyntaxSiteContracts: lexical path grammar.
  (void)&acir::ac::detail::verifyLexicalPath;
  // SourceValueSemantics: value opcode grammar.
  (void)&acir::ac::detail::parseValueOpcode;
  // ACIRStaticEvaluation: static expression and record resolution.
  (void)&acir::ac::detail::evaluateSourceStaticExpr;
  // SourceStaticRecordConstructors: source-static record constructor
  // evaluation over func bodies.
  (void)&acir::ac::detail::evaluateSourceStaticRecordConstructor;
  // SourceStaticDomains: source domain resolution and matching.
  (void)&acir::ac::detail::resolveSourceDomain;
  // StateDeclarationContracts: declaration placement contracts.
  (void)&acir::ac::verifyRoutedLogicalStatePlacement;
  // SourceIdentifier: Python identifier grammar leaf.
  (void)&acir::ac::detail::verifyPythonAstIdentifier;
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

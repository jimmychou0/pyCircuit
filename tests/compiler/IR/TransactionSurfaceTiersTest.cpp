// Batch-1 acceptance (r8 review): the same minimal transaction design
// proves the tier contract — the symbolic template publishes standalone,
// the legal actual binding completes routing, the illegal actual binding
// is rejected at the routing preparation, and a definite payload-type
// error is already rejected at the source stage.
#include "pycircuit/Dialect/ACIR/ACIRDialect.h"
#include "pycircuit/Dialect/ACIR/SourceUnitValidation.h"
#include "pycircuit/Transforms/Passes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Pass/PassManager.h"
#include "llvm/Support/raw_ostream.h"
#include "gtest/gtest.h"

#include <string>

namespace {
namespace ac = acir::ac;
using namespace mlir;

const char *types = R"mlir(
#w1 = #ac.static_expr<{kind = "literal", location = {path = "tiers.py", line = 1 : i64, column = 1 : i64, end_line = 1 : i64, end_column = 2 : i64}, origin = {site = {definition = @unit.Bank, ast_path = []}, expansion = []}, value = {kind = "integer", value = #ac.math_int<1>}}>
#w2 = #ac.static_expr<{kind = "literal", location = {path = "tiers.py", line = 1 : i64, column = 1 : i64, end_line = 1 : i64, end_column = 2 : i64}, origin = {site = {definition = @unit.Bank, ast_path = []}, expansion = []}, value = {kind = "integer", value = #ac.math_int<2>}}>
#w8 = #ac.static_expr<{kind = "literal", location = {path = "tiers.py", line = 1 : i64, column = 1 : i64, end_line = 1 : i64, end_column = 2 : i64}, origin = {site = {definition = @unit.Bank, ast_path = []}, expansion = []}, value = {kind = "integer", value = #ac.math_int<8>}}>
#n = #ac.static_expr<{kind = "reference", location = {path = "tiers.py", line = 1 : i64, column = 1 : i64, end_line = 1 : i64, end_column = 2 : i64}, origin = {site = {definition = @unit.Bank, ast_path = []}, expansion = []}, ref = {kind = "parameter", owner = @unit.Bank, name = "N"}}>
!b1 = !ac.bits<#w1>
!b2 = !ac.bits<#w2>
!b8 = !ac.bits<#w8>
!lanes = !ac.table<[#n], !b8>
!two = !ac.table<[#w2], !b8>
!three = !ac.table<[#w2], !b1>
)mlir";

// The generic design: one state declaration, one Method-like rule surface.
// The read result type derives from the state payload; a wrong payload on
// the read is a DEFINITE error (independent of any binding).
std::string bank(const char *readResult) {
  return std::string("\"ac.module\"() ({\n"
                     "^bb0(%clk: !b1, %rst: !b1, %enable: !b1, %data: !b8):\n"
                     "    %state = \"ac.state\"(%clk, %rst, %lanes_init) ") +
         "{\"instance_name = \"entries\", payload = !lanes, occurrence = "
         "{site = {definition = @unit.Bank, ast_path = []}, expansion = "
         "[]}} : (!lanes) -> !lanes\n" +
         "    \"ac.yield\"() : () -> ()\n"
         "  }) {sym_name = \"unit.Bank\", source_owner = {package = \"\", "
         "path = \"tiers.py\"}, parameters = [{name = \"N\", type = "
         "!ac.math_int}], type_parameters = [], function_type = (!b1, !b1, "
         "!b1, !b8) -> (), input_names = [\"clk\", \"rst\", \"enable\", "
         "\"data\"], output_names = []} : () -> ()\n";
}

class TransactionSurfaceTiersTest : public ::testing::Test {
protected:
  MLIRContext context;
  std::string diagnostics;
  void SetUp() override { context.loadDialect<ac::ACIRDialect>(); }
};

} // namespace

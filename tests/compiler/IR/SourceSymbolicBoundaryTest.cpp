// r9 §4 acceptance pair: a legal symbolic-template source unit passes the
// source-stage entry with its binding obligations DEFERRED, and the same
// design with an actual (default-supplied) binding is rejected with the
// family's pinned diagnostic at the tier that owns resolution.
#include "pycircuit/Dialect/ACIR/ACIRDialect.h"
#include "pycircuit/Dialect/ACIR/SourceUnitValidation.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Parser/Parser.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "gtest/gtest.h"

#include <string>

namespace {
namespace ac = acir::ac;
using namespace mlir;

const char *types = R"mlir(
#w8 = #ac.static_expr<{kind = "literal", location = {path = "unit.py", line = 1 : i64, column = 1 : i64, end_line = 1 : i64, end_column = 2 : i64}, origin = {site = {definition = @unit.Generic, ast_path = []}, expansion = []}, value = {kind = "integer", value = #ac.math_int<8>}}>
#n = #ac.static_expr<{kind = "reference", location = {path = "unit.py", line = 1 : i64, column = 1 : i64, end_line = 1 : i64, end_column = 2 : i64}, origin = {site = {definition = @unit.Generic, ast_path = []}, expansion = []}, ref = {kind = "parameter", owner = @unit.Generic, name = "N"}}>
#zero = #ac.static_expr<{kind = "literal", location = {path = "unit.py", line = 1 : i64, column = 1 : i64, end_line = 1 : i64, end_column = 2 : i64}, origin = {site = {definition = @unit.Generic, ast_path = []}, expansion = []}, value = {kind = "integer", value = #ac.math_int<0>}}>
!b8 = !ac.bits<#w8>
!lanes = !ac.table<[#n], !b8>
)mlir";

std::string generic(bool boundDefault) {
  std::string parameters =
      boundDefault
          ? "[{name = \"N\", type = !ac.math_int, default = #zero}]"
          : "[{name = \"N\", type = !ac.math_int}]";
  return "\"ac.module\"() ({\n"
         "^bb0(%x: !b8):\n"
         "    %lanes = \"ac.table.splat\"(%x) {shape = [#n]} : (!b8) -> !lanes\n"
         "    \"ac.yield\"(%lanes) : (!lanes) -> ()\n"
         "  }) {sym_name = \"unit.Generic\", source_owner = {package = \"\", "
         "path = \"unit.py\"}, parameters = " +
         parameters +
         ", type_parameters = [], function_type = (!b8) -> !lanes, "
         "input_names = [\"x\"], output_names = [\"lanes\"]} : () -> ()\n";
}

class SourceSymbolicBoundaryTest : public ::testing::Test {
protected:
  MLIRContext context;
  std::string diagnostics;
  void SetUp() override { context.loadDialect<ac::ACIRDialect>(); }
  DictionaryAttr owner(StringRef path = "unit.py") {
    Builder b(&context);
    return b.getDictionaryAttr({b.getNamedAttr("package", b.getStringAttr("")),
                                b.getNamedAttr("path", b.getStringAttr(path))});
  }
  DictionaryAttr origin(StringRef symbol) {
    Builder b(&context);
    return b.getDictionaryAttr(
        {b.getNamedAttr(
             "site", b.getDictionaryAttr(
                         {b.getNamedAttr("definition", FlatSymbolRefAttr::get(
                                                           &context, symbol)),
                          b.getNamedAttr("ast_path", b.getArrayAttr({}))})),
         b.getNamedAttr("expansion", b.getArrayAttr({}))});
  }
  DictionaryAttr site(StringRef path = "unit.py") {
    Builder b(&context);
    NamedAttrList span;
    span.append("path", b.getStringAttr(path));
    for (StringRef key : {"line", "column", "end_line", "end_column"})
      span.append(key, b.getI64IntegerAttr(key == "end_column" ? 2 : 1));
    return b.getDictionaryAttr(
        {b.getNamedAttr("ast_path", b.getArrayAttr({})),
         b.getNamedAttr("location", span.getDictionary(&context))});
  }
  OwningOpRef<mlir::ModuleOp> parse(StringRef content) {
    auto unit = parseSourceString<mlir::ModuleOp>(
        std::string(types) + "module {\n" + content.str() + "\n}", &context);
    if (!unit)
      return {};
    Builder b(&context);
    (*unit)->setAttr("ac.stage", b.getStringAttr("source"));
    (*unit)->setAttr("ac.unit_kind", b.getStringAttr("implementation"));
    (*unit)->setAttr("ac.source_owner", owner());
    SmallVector<Attribute> owners{owner()};
    std::vector<std::pair<std::string, Attribute>> exports;
    for (Operation &op : unit->getBody()->getOperations()) {
      auto symbol = SymbolTable::getSymbolName(&op);
      if (!symbol)
        continue;
      op.setAttr("ac.origin", origin(symbol.getValue()));
      op.setAttr("ac.declaration_role", b.getStringAttr("definition"));
      std::string name = symbol.getValue().rsplit('.').second.str();
      exports.emplace_back(
          name, b.getDictionaryAttr(
                    {b.getNamedAttr("name", b.getStringAttr(name)),
                     b.getNamedAttr("target",
                                    FlatSymbolRefAttr::get(&context,
                                                           symbol.getValue())),
                     b.getNamedAttr("site", site())}));
    }
    SmallVector<Attribute> rows;
    for (const auto &entry : exports)
      rows.push_back(entry.second);
    (*unit)->setAttr("ac.interfaces", b.getArrayAttr(owners));
    (*unit)->setAttr("ac.exports", b.getArrayAttr(rows));
    (*unit)->setAttr("ac.import_bindings", b.getArrayAttr({}));
    return unit;
  }
};

TEST_F(SourceSymbolicBoundaryTest,
       SymbolicTemplatePassesTheSourceStageWithDeferredObligations) {
  diagnostics.clear();
  mlir::ScopedDiagnosticHandler handler(&context, [&](Diagnostic &error) {
    llvm::raw_string_ostream stream(diagnostics);
    error.print(stream);
    return success();
  });
  auto unit = parse(generic(/*boundDefault=*/false));
  ASSERT_TRUE(unit) << diagnostics;
  ASSERT_TRUE(succeeded(mlir::verify(*unit))) << diagnostics;
  auto error = [&] { return unit->emitError(); };
  // The generic extent stays unbound: the source-stage entry must hold the
  // resolution obligation instead of rejecting the legal template (r9 §4).
  EXPECT_TRUE(succeeded(ac::verifySourceBodyStructure(*unit, error)))
      << diagnostics;
}

TEST_F(SourceSymbolicBoundaryTest,
       ActualZeroBindingIsRejectedAtTheResolvingTier) {
  diagnostics.clear();
  mlir::ScopedDiagnosticHandler handler(&context, [&](Diagnostic &error) {
    llvm::raw_string_ostream stream(diagnostics);
    error.print(stream);
    return success();
  });
  auto unit = parse(generic(/*boundDefault=*/true));
  ASSERT_TRUE(unit) << diagnostics;
  ASSERT_TRUE(succeeded(mlir::verify(*unit))) << diagnostics;
  auto error = [&] { return unit->emitError(); };
  // The default supplies the actual binding, so the same design resolves to
  // a zero table extent and the collection entry rejects it with the
  // family's pinned diagnostic.
  EXPECT_TRUE(failed(ac::verifySourceBodyStructure(*unit, error)));
  EXPECT_NE(diagnostics.find("hardware static value must be positive and fit "
                             "u64"),
            std::string::npos)
      << diagnostics;
}

} // namespace

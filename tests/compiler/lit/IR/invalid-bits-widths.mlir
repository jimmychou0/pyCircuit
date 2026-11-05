// RUN: %pycircuit_opt %s --split-input-file --ac-verify-hardware --verify-diagnostics
// The symbolic-width equivalence rejections moved to the staged tiers (r3
// ACIRBitsOps row); the pinned messages are the bound-tier texts from the
// resolved dispatch.
#w5 = #ac.static_expr<{kind = "literal", location = {path = "top.py", line = 1 : i64, column = 1 : i64, end_line = 1 : i64, end_column = 2 : i64}, origin = {site = {definition = @top, ast_path = []}, expansion = []}, value = {kind = "integer", value = #ac.math_int<5>}}>
!b5 = !ac.bits<#w5>
#w8 = #ac.static_expr<{kind = "literal", location = {path = "top.py", line = 1 : i64, column = 1 : i64, end_line = 1 : i64, end_column = 2 : i64}, origin = {site = {definition = @top, ast_path = []}, expansion = []}, value = {kind = "integer", value = #ac.math_int<8>}}>
!b8 = !ac.bits<#w8>
#w1 = #ac.static_expr<{kind = "literal", location = {path = "case_eq.py", line = 1 : i64, column = 1 : i64, end_line = 1 : i64, end_column = 2 : i64}, origin = {site = {definition = @top, ast_path = []}, expansion = []}, value = {kind = "integer", value = #ac.math_int<1>}}>
!b1 = !ac.bits<#w1>
module {
  "ac.module"() ({
  ^bb0(%a: !b5, %b: !b8):
    // expected-error @+1 {{bound bit operands and result must have equal widths}}
    %bad = "ac.bits.binary"(%a, %b) {opcode = "xor"} : (!b5, !b8) -> !b5
    "ac.yield"(%bad) : (!b5) -> ()
  }) {sym_name = "top", source_owner = {package = "", path = "top.py"}, parameters = [], type_parameters = [], function_type = (!b5, !b8) -> !b5, input_names = ["a", "b"], output_names = ["result"]} : () -> ()
  "ac.system"() {entry = {callee = @top, parameters = [], type_arguments = []}, domain = "default"} : () -> ()
}

// -----

module {
  "ac.module"() ({
  ^bb0(%lhs: !b1, %rhs: !b8):
    // expected-error @+1 {{bound comparison requires equal operands and width-one result}}
    %bad = "ac.bits.compare"(%lhs, %rhs) {predicate = "case_eq"} : (!b1, !b8) -> !b1
    "ac.yield"(%bad) : (!b1) -> ()
  }) {sym_name = "top", source_owner = {package = "", path = "case_eq.py"}, parameters = [], type_parameters = [], function_type = (!b1, !b8) -> !b1, input_names = ["lhs", "rhs"], output_names = ["result"]} : () -> ()
  "ac.system"() {entry = {callee = @top, parameters = [], type_arguments = []}, domain = "default"} : () -> ()
}

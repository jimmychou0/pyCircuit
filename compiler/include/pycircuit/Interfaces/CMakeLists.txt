set(LLVM_TARGET_DEFINITIONS LogicalStateInterfaces.td)
mlir_tablegen(LogicalStateInterfaces.h.inc -gen-op-interface-decls)
mlir_tablegen(LogicalStateInterfaces.cpp.inc -gen-op-interface-defs)
add_public_tablegen_target(LogicalStateInterfacesIncGen)

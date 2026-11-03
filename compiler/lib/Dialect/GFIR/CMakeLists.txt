# GFIR registers in the same acir library namespace as the common dialect but
# owns only the routed stage's logical-state operations. It links the common
# ac IR (payload types, declaration contracts, shared state interfaces) and
# never the staged package analysis for registration.
add_acir_library(GFIRDialect SOURCES
  GFIRDialect.cpp
  GFIROps.cpp
)
add_dependencies(GFIRDialect
  GFIRDialectIncGen
  GFIROpsIncGen
)
target_link_libraries(GFIRDialect PUBLIC
  ACIRDialect
  PycircuitStateInterfaces
  MLIRIR
  MLIRSideEffectInterfaces
)
target_include_directories(GFIRDialect PRIVATE
  "${PYCIRCUIT_SOURCE_DIR}/lib"
)

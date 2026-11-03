add_acir_library(PycircuitStateInterfaces SOURCES
  LogicalStateInterfaces.cpp
)
add_dependencies(PycircuitStateInterfaces
  LogicalStateInterfacesIncGen
)

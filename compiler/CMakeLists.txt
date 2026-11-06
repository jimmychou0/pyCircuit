cmake_minimum_required(VERSION 3.25)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(pycircuit VERSION 6.1.0 LANGUAGES C CXX)
endif()

set(PYCIRCUIT_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(PYCIRCUIT_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}")
get_filename_component(PYCIRCUIT_REPO_ROOT "${PYCIRCUIT_SOURCE_DIR}/.." ABSOLUTE)

if(NOT TARGET MLIRIR)
  find_package(LLVM 22.1.8 EXACT CONFIG REQUIRED)
  find_package(MLIR 22.1.8 EXACT CONFIG REQUIRED)
endif()

list(APPEND CMAKE_MODULE_PATH "${LLVM_DIR}" "${MLIR_DIR}"
     "${PYCIRCUIT_SOURCE_DIR}/cmake")
include(TableGen)
include(AddLLVM)
include(AddMLIR)
include(AddACIR)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

include_directories(SYSTEM ${LLVM_INCLUDE_DIRS} ${MLIR_INCLUDE_DIRS})
include_directories("${PYCIRCUIT_SOURCE_DIR}/include" "${PYCIRCUIT_BINARY_DIR}/include")
add_definitions(${LLVM_DEFINITIONS})

add_library(pycircuit_project_options INTERFACE)
target_compile_features(pycircuit_project_options INTERFACE cxx_std_20)
# Compiler consumers must match LLVM's RTTI configuration. Keep this on the
# compiler interface so standalone Runtime targets retain their own policy.
if(NOT LLVM_ENABLE_RTTI)
  if(MSVC)
    target_compile_options(pycircuit_project_options INTERFACE "$<$<COMPILE_LANGUAGE:CXX>:/GR->")
  else()
    target_compile_options(pycircuit_project_options INTERFACE "$<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>")
  endif()
endif()

option(PYCIRCUIT_BUILD_TESTING "Build isolated source-unit tests" OFF)
if(PYCIRCUIT_BUILD_TESTING)
  include(CTest)
  enable_testing()
endif()

add_subdirectory(include)
add_subdirectory(lib)
add_subdirectory(tools)

if(PYCIRCUIT_BUILD_TESTING)
  add_subdirectory(
    "${PYCIRCUIT_REPO_ROOT}/tests/compiler/IR"
    "${PYCIRCUIT_BINARY_DIR}/tests/source-unit"
  )
endif()


# ---------------------------------------------------------------------------
# Layer-direction enforcement (B2 step-2 design r9 §4): resolve each layer
# target's transitive INTERFACE_LINK_LIBRARIES at configure time and fail
# on forbidden edges. Generator-expression values are stripped; ALIAS
# targets resolve; a visited set guards cycles.
function(_pyc_link_closure target out)
  set(acc "")
  set(pending "${target}")
  set(seen "")
  while(pending)
    list(POP_FRONT pending head)
    if(head IN_LIST seen)
      continue()
    endif()
    list(APPEND seen "${head}")
    list(APPEND acc "${head}")
    if(NOT TARGET "${head}")
      continue()
    endif()
    get_target_property(iface "${head}" INTERFACE_LINK_LIBRARIES)
    # The seed target's own private link line is examined too (IX): a
    # forbidden edge must not survive as a non-interface dependency of the
    # layer target itself.
    get_target_property(priv "${head}" LINK_LIBRARIES)
    foreach(entries "${iface}" "${priv}")
      if(entries)
        foreach(entry IN LISTS entries)
          # Generator expressions may carry real target names
          # ($<LINK_ONLY:ACIRDialect>, $<BUILD_INTERFACE:x>); strip the
          # wrappers and examine every remaining token so hidden forbidden
          # edges cannot survive inside an expression.
          string(REGEX REPLACE "\\$<[^:]*:" "\n" tokens "${entry}")
          string(REGEX REPLACE ">" "\n" tokens "${tokens}")
          foreach(token IN LISTS tokens)
            string(STRIP "${token}" token)
            if(NOT token MATCHES "^\\$<.*")
              string(REGEX MATCHALL "[A-Za-z0-9_.:-]+" names "${token}")
              foreach(name IN LISTS names)
                list(APPEND pending "${name}")
              endforeach()
            endif()
          endforeach()
        endforeach()
      endif()
    endforeach()
  endwhile()
  set("${out}" "${acc}" PARENT_SCOPE)
endfunction()

function(pycircuit_verify_layering)
  set(forbidden
    "GFIRDialect:ACIRDialect"
    "GFIRDialect:ACIRAnalysis"
    "ACIRCore:ACIRDialect"
    "ACIRCore:ACIRAnalysis"
    "ACIRDialect:ACIRAnalysis"
    "ACIRAnalysis:ACIRTransforms"
    "ACIRAnalysis:ACIRCodegen")
  foreach(pair IN LISTS forbidden)
    string(REPLACE ":" ";" ends "${pair}")
    list(GET ends 0 lower)
    list(GET ends 1 upper)
    if(NOT TARGET "${lower}")
      continue()
    endif()
    _pyc_link_closure("${lower}" closure)
    if("${upper}" IN_LIST closure)
      message(FATAL_ERROR
        "layering violation: ${lower} transitively links ${upper}")
    endif()
  endforeach()
  message(STATUS "verify-layering: layer directions OK")
endfunction()

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
    # Normalize ALIAS targets first: an indirect edge through an alias must
    # be attributed to the aliased layer target (r8 review fix).
    if(TARGET "${head}")
      get_target_property(_pyc_aliased "${head}" ALIASED_TARGET)
      if(_pyc_aliased)
        set(head "${_pyc_aliased}")
      endif()
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
          # Reduce generator expressions STRUCTURALLY (r8 review batch 3
          # follow-up): an expression is $<OP:BODY> with OP ending at the
          # first depth-0 colon. Condition operators (CONFIG, TARGET_EXISTS,
          # ...) do not link their arguments — only the BODY is the
          # conditionally linked result; LINK_ONLY/BUILD_INTERFACE/
          # INSTALL_INTERFACE carry their whole argument as content; and a
          # TARGET_PROPERTY (or any other value-producing operator) cannot
          # be resolved statically, so the configure fails with the exact
          # target and expression instead of silently missing the edge.
          set(remainder "${entry}")
          while(remainder MATCHES "\\$<")
            string(FIND "${remainder}" "$<" openAt)
            string(LENGTH "${remainder}" remainderLength)
            string(SUBSTRING "${remainder}" 2 -1 afterOpen)
            set(depth 1)
            set(cursor 2)
            set(closeAt -1)
            string(LENGTH "${afterOpen}" scanLength)
            set(scan 0)
            while(scan LESS scanLength)
              string(SUBSTRING "${afterOpen}" ${scan} 2 pair)
              string(SUBSTRING "${afterOpen}" ${scan} 1 char)
              if(pair STREQUAL "$<")
                math(EXPR depth "${depth} + 1")
                math(EXPR scan "${scan} + 2")
                continue()
              endif()
              if(char STREQUAL ">")
                math(EXPR depth "${depth} - 1")
                if(depth EQUAL 0)
                  math(EXPR closeAt "${cursor} + ${scan}")
                  break()
                endif()
              endif()
              math(EXPR scan "${scan} + 1")
            endwhile()
            if(closeAt EQUAL -1)
              message(FATAL_ERROR
                "layering walk: ${head} carries the unbalanced generator "
                "expression '${entry}'")
            endif()
            math(EXPR innerStart 2)
            math(EXPR innerLength "${closeAt} - 2")
            string(SUBSTRING "${remainder}" 2 ${innerLength} inner)
            # Split the inner text at the first depth-0 colon.
            set(iDepth 1)
            set(splitAt -1)
            string(LENGTH "${inner}" innerLength2)
            set(scan 0)
            while(scan LESS innerLength2)
              string(SUBSTRING "${inner}" ${scan} 2 pair)
              string(SUBSTRING "${inner}" ${scan} 1 char)
              if(pair STREQUAL "$<")
                math(EXPR iDepth "${iDepth} + 1")
                math(EXPR scan "${scan} + 2")
                continue()
              endif()
              if(char STREQUAL ">")
                math(EXPR iDepth "${iDepth} - 1")
              endif()
              if(char STREQUAL ":" AND iDepth EQUAL 1)
                set(splitAt ${scan})
                break()
              endif()
              math(EXPR scan "${scan} + 1")
            endwhile()
            if(splitAt EQUAL -1)
              set(op "${inner}")
              set(body "")
            else()
              string(SUBSTRING "${inner}" 0 ${splitAt} op)
              math(EXPR bodyStart "${splitAt} + 1")
              math(EXPR bodyLength "${innerLength2} - ${bodyStart}")
              string(SUBSTRING "${inner}" ${bodyStart} ${bodyLength} body)
            endif()
            if(op MATCHES "^TARGET_PROPERTY")
              message(FATAL_ERROR
                "layering walk: ${head} carries '${entry}' whose "
                "TARGET_PROPERTY result cannot be resolved statically; "
                "replace it on a layer link line")
            endif()
            if(NOT op MATCHES "^(LINK_ONLY|BUILD_INTERFACE|INSTALL_INTERFACE)$")
              # A condition: drop the condition text, keep only the result.
              set(replacement "${body}")
            else()
              set(replacement "${inner}")
            endif()
            string(LENGTH "${remainder}" tailLength)
            math(EXPR tailStart "${closeAt} + 1")
            math(EXPR tailLength "${tailLength} - ${tailStart}")
            string(SUBSTRING "${remainder}" ${tailStart} ${tailLength} tail)
            set(remainder "${replacement} ${tail}")
          endwhile()
          # Tokens keep namespace separators (::) inside a target name but
          # split on single colons, so a "LINK_ONLY:X" or ":X" fragment can
          # never pass as the target itself while a "ns::X" dependency stays
          # one name (r8 review batch 3).
          string(REGEX MATCHALL "[A-Za-z0-9_.-]+(::[A-Za-z0-9_.-]+)*" names "${remainder}")
          foreach(name IN LISTS names)
            if(TARGET "${name}")
              get_target_property(_pyc_dep_aliased "${name}" ALIASED_TARGET)
              if(_pyc_dep_aliased)
                set(name "${_pyc_dep_aliased}")
              endif()
              list(APPEND pending "${name}")
            endif()
          endforeach()
          # A layer target mentioned inside an entry that did not resolve to
          # a dependency must not slip through silently (r8 review batch 3).
          foreach(_pyc_layer IN ITEMS GFIRDialect ACIRDialect ACIRAnalysis ACIRCore)
            if("${entry}" MATCHES "(^|[^A-Za-z0-9_:.${_pyc_layer}])${_pyc_layer}([^A-Za-z0-9_:.]|$)")
              set(_pyc_resolved 0)
              foreach(name IN LISTS names)
                if(name MATCHES "${_pyc_layer}")
                  set(_pyc_resolved 1)
                endif()
              endforeach()
              if(NOT _pyc_resolved)
                message(WARNING
                  "layering walk: ${head} carries '${entry}' which mentions "
                  "layer target ${_pyc_layer} but resolved no dependency for "
                  "it; inspect this expression manually")
              endif()
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

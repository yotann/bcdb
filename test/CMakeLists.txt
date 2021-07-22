configure_lit_site_cfg(
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.cfg.py.in
  ${CMAKE_CURRENT_BINARY_DIR}/lit.cfg.py
)
configure_lit_site_cfg(
  ${CMAKE_CURRENT_SOURCE_DIR}/unit/lit.cfg.py.in
  ${CMAKE_CURRENT_BINARY_DIR}/unit/lit.cfg.py
)

set(LLVM_EXTERNAL_LIT ${CMAKE_SOURCE_DIR}/third_party/lit/lit.py CACHE STRING "Command used to spawn lit")
set(LLVM_LIT_ARGS "-sv" CACHE STRING "Default options for lit")

# Check we have the FileCheck utility.
find_program(FILECHECK_PATH FileCheck HINTS ${LLVM_TOOLS_BINARY_DIR})
if (NOT FILECHECK_PATH)
  message(STATUS "LLVM Tools path: ${LLVM_TOOLS_BINARY_DIR}")
  message(FATAL_ERROR "
     Unable to find required 'FileCheck' utility.
     Check that LLVM_INSTALL_UTILS was enabled when you built LLVM.")
else()
  message(STATUS "Found FileCheck: ${FILECHECK_PATH}")
endif()

add_subdirectory(bcdb)
add_subdirectory(guided_linking)
add_subdirectory(outlining)

add_lit_testsuite(check-unit "Running unit tests"
  ${CMAKE_CURRENT_BINARY_DIR}/unit
  DEPENDS UnitTests
)

# Now that we've added all test suites, define the combined "make check"
# command.
get_property(LLVM_LIT_TESTSUITES GLOBAL PROPERTY LLVM_LIT_TESTSUITES)
get_property(LLVM_LIT_DEPENDS GLOBAL PROPERTY LLVM_LIT_DEPENDS)
add_lit_target(check "Running all tests"
  ${LLVM_LIT_TESTSUITES}
  DEPENDS ${LLVM_LIT_DEPENDS}
)

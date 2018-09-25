configure_lit_site_cfg(
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.cfg.py.in
  ${CMAKE_CURRENT_BINARY_DIR}/lit.cfg.py
)

set(LIT_COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_SOURCE_DIR}/utils/lit-0.7.0/lit.py)

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

add_subdirectory(Align)

# Now that we've added all test suites, define the combined "make check"
# command.
get_property(LLVM_LIT_TESTSUITES GLOBAL PROPERTY LLVM_LIT_TESTSUITES)
get_property(LLVM_LIT_DEPENDS GLOBAL PROPERTY LLVM_LIT_DEPENDS)
add_lit_target(check "Running all tests"
  ${LLVM_LIT_TESTSUITES}
  DEPENDS ${LLVM_LIT_DEPENDS}
)

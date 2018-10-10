add_lit_testsuite(check-split "Running bc-split/bc-join tests"
  ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS bc-split bc-join
)

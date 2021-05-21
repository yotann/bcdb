add_lit_testsuite(check-sqlite "Running bcdb tests on SQLite"
  ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS bcdb memodb
)

if(WITH_LEVELDB)
  add_lit_testsuite(check-leveldb "Running bcdb tests on LevelDB"
    ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS bcdb
  )
endif(WITH_LEVELDB)

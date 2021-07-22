if(WITH_ROCKSDB)
  add_lit_testsuite(check-rocksdb "Running bcdb tests on RocksDB"
    ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS bcdb memodb
  )
endif(WITH_ROCKSDB)

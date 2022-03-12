if(WITH_ROCKSDB)
  # Require RocksDB because shelltest.py uses it when --with-store=client.
  add_lit_testsuite(check-client "Running bcdb tests on HTTP client"
    ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS memodb memodb-server bcdb
  )
endif(WITH_ROCKSDB)

set(LLVM_LINK_COMPONENTS
  Support
)
add_llvm_library(MemoDB
  CAR.cpp
  CID.cpp
  Multibase.cpp
  Node.cpp
  RocksDB.cpp
  SQLite.cpp
  Store.cpp
  Support.cpp
)
target_include_directories(MemoDB PRIVATE
  ${LIBSODIUM_INCLUDE_DIRS}
  ${SQLITE3_INCLUDE_DIRS}
)
target_link_libraries(MemoDB PRIVATE
  ${LIBSODIUM_LIBRARIES}
  ${SQLITE3_LIBRARIES}
)
if(WITH_ROCKSDB)
  target_link_libraries(MemoDB PRIVATE RocksDB::rocksdb)
  add_compile_definitions(BCDB_WITH_ROCKSDB=1)
endif(WITH_ROCKSDB)

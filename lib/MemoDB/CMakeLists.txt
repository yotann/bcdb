set(LLVM_LINK_COMPONENTS
  Support
)
add_llvm_library(MemoDB
  CAR.cpp
  CID.cpp
  HTTP.cpp
  Multibase.cpp
  NNG.cpp
  Node.cpp
  RocksDB.cpp
  Server.cpp
  SQLite.cpp
  Store.cpp
  Support.cpp
)
target_link_libraries(MemoDB PRIVATE
  Sodium::LibSodium
  SQLite::SQLite3
)
if(WITH_ROCKSDB)
  target_link_libraries(MemoDB PRIVATE RocksDB::rocksdb)
  add_compile_definitions(BCDB_WITH_ROCKSDB=1)
endif(WITH_ROCKSDB)
if(WITH_NNG)
  add_compile_definitions(BCDB_WITH_NNG=1)
endif(WITH_NNG)

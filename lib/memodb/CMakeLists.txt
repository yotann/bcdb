set(LLVM_LINK_COMPONENTS
  Support
)
add_llvm_library(libmemodb
  CAR.cpp
  CID.cpp
  Evaluator.cpp
  HTTP.cpp
  Multibase.cpp
  Node.cpp
  RocksDB.cpp
  Server.cpp
  SQLite.cpp
  Store.cpp
  URI.cpp
)
target_link_libraries(libmemodb PRIVATE
  Sodium::LibSodium
  SQLite::SQLite3
)
if(WITH_ROCKSDB)
  target_link_libraries(libmemodb PRIVATE RocksDB::rocksdb)
  add_compile_definitions(BCDB_WITH_ROCKSDB=1)
endif(WITH_ROCKSDB)

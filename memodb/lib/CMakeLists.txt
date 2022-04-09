include_directories(${CMAKE_CURRENT_BINARY_DIR})
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/bcdb-version.h.in"
  "${CMAKE_CURRENT_BINARY_DIR}/bcdb-version.h")

set(LLVM_LINK_COMPONENTS
  Support
)
add_llvm_library(libmemodb
  CAR.cpp
  CBOREncoder.cpp
  CID.cpp
  Client.cpp
  Evaluator.cpp
  Funcs.cpp
  HTTP.cpp
  JSONEncoder.cpp
  Multibase.cpp
  Node.cpp
  NodeVisitor.cpp
  Request.cpp
  RocksDB.cpp
  Server.cpp
  SQLite.cpp
  Store.cpp
  ToolSupport.cpp
  URI.cpp
)
target_link_libraries(libmemodb PRIVATE
  Boost::Beast
  Boost::fiber
  Sodium::LibSodium
  SQLite::SQLite3
)
if(WITH_ROCKSDB)
  target_link_libraries(libmemodb PRIVATE RocksDB::rocksdb)
  add_compile_definitions(BCDB_WITH_ROCKSDB=1)
endif(WITH_ROCKSDB)

add_custom_command(OUTPUT scripting_init.inc
  COMMAND cp ${CMAKE_CURRENT_SOURCE_DIR}/scripting_init.js scripting_init.js
  COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../../utils/xxd.py scripting_init.js scripting_init.inc
  DEPENDS scripting_init.js)
add_custom_target(scripting_init DEPENDS scripting_init.inc)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

set(LLVM_LINK_COMPONENTS
  Support
)
add_llvm_library(MemoDB
  CAR.cpp
  CID.cpp
  MemoDB.cpp
  Multibase.cpp
  RocksDB.cpp
  SQLite.cpp
  Scripting.cpp
  Node.cpp
)
target_include_directories(MemoDB PRIVATE
  ${LIBSODIUM_INCLUDE_DIRS}
  ${SQLITE3_INCLUDE_DIRS}
)
target_link_libraries(MemoDB PRIVATE
  ${LIBSODIUM_LIBRARIES}
  ${SQLITE3_LIBRARIES}
  Duktape
  Linenoise
  RocksDB::rocksdb
)
add_dependencies(MemoDB scripting_init)

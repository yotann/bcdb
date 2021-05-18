set(LLVM_LINK_COMPONENTS
  Support
)
add_llvm_library(MemoDB
  LevelDB.cpp
  MemoDB.cpp
  SQLite.cpp
)
target_include_directories(MemoDB PRIVATE
  ${LIBSODIUM_INCLUDE_DIRS}
  ${SQLITE3_INCLUDE_DIRS}
)
target_link_libraries(MemoDB PRIVATE
  ${LIBSODIUM_LIBRARIES}
  ${SQLITE3_LIBRARIES}
)
if(WITH_LEVELDB)
  target_include_directories(MemoDB PRIVATE ${LEVELDB_INCLUDE_DIRS})
  target_link_libraries(MemoDB PRIVATE ${LEVELDB_LIBRARIES})
  add_compile_definitions(BCDB_WITH_LEVELDB=1)
endif(WITH_LEVELDB)

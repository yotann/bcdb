set(LLVM_LINK_COMPONENTS
  Support
)
add_llvm_library(MemoDB
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

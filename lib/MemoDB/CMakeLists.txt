add_llvm_library(MemoDB
  Git.cpp
  MemoDB.cpp
  SQLite.cpp
)
target_include_directories(MemoDB PRIVATE
  ${LIBGIT2_INCLUDE_DIRS}
  ${LIBSODIUM_INCLUDE_DIRS}
  ${SQLITE3_INCLUDE_DIRS}
)
target_link_libraries(MemoDB PRIVATE
  ${LIBGIT2_LIBRARIES}
  ${LIBSODIUM_LIBRARIES}
  ${SQLITE3_LIBRARIES}
)

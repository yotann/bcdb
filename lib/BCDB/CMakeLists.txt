add_llvm_library(BCDB
  BCDB.cpp
)
target_link_libraries(BCDB PRIVATE
  BCDBBitcode
  BCDBSplit
  MemoDB
)

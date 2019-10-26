add_llvm_library(BCDB
  BCDB.cpp
  Merge.cpp
  Mux.cpp
  Mux2.cpp
)
target_link_libraries(BCDB PRIVATE
  BCDBBitcode
  BCDBSplit
  MemoDB
)

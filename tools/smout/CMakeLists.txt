set(LLVM_LINK_COMPONENTS
  IRReader
  Support
)
add_llvm_tool(smout
  smout.cpp
)
target_compile_options(smout PRIVATE "${OpenMP_CXX_FLAGS}")
target_link_libraries(smout PRIVATE "${OpenMP_CXX_FLAGS}")
target_link_libraries(smout PRIVATE
  BCDB
  BCDBOutlining
)

set(LLVM_LINK_COMPONENTS
  IRReader
  Support
)
add_llvm_tool(smout
  smout.cpp
)
target_link_libraries(smout PRIVATE
  BCDB
  BCDBOutlining
)

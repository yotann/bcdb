set(LLVM_LINK_COMPONENTS
  IRReader
  Support
)
add_llvm_tool(smout
  smout.cpp
)
set_target_properties(smout PROPERTIES COMPILE_FLAGS -fopenmp)
set_target_properties(smout PROPERTIES LINK_FLAGS -fopenmp)
target_link_libraries(smout PRIVATE
  BCDB
  BCDBOutlining
)

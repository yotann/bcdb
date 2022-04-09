set(LLVM_LINK_COMPONENTS
  IRReader
  Support
)
add_llvm_tool(bc-split
  bc-split.cpp
)
target_link_libraries(bc-split PRIVATE
  libbcdb
)

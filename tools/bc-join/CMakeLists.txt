set(LLVM_LINK_COMPONENTS
  IRReader
  Support
)
add_llvm_tool(bc-join
  bc-join.cpp
)
target_link_libraries(bc-join PRIVATE
  libbcdb
)

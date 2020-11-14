set(LLVM_LINK_COMPONENTS
  IRReader
  Support
)
add_llvm_tool(bcdb
  bcdb.cpp
)
target_link_libraries(bcdb PRIVATE
  BCDB
)

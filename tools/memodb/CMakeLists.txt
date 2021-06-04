set(LLVM_LINK_COMPONENTS
  Support
)
add_llvm_tool(memodb
  memodb.cpp
)
target_link_libraries(memodb PRIVATE
  Duktape
  MemoDB
)

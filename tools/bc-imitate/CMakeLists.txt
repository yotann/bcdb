set(LLVM_LINK_COMPONENTS
  BitWriter
  IRReader
  Object
  Support
)
add_llvm_tool(bc-imitate
  bc-imitate.cpp
)
target_link_libraries(bc-imitate PRIVATE
  libbcdb
)

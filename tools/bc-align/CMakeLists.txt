set(LLVM_LINK_COMPONENTS
  Support
)
add_llvm_tool(bc-align
  bc-align.cpp
)
target_link_libraries(bc-align BCDBBitcode)

set(LLVM_LINK_COMPONENTS
  BitReader
  Support
)
add_llvm_tool(bc-align
  BitstreamReader.cpp
  bc-align.cpp
)

set(LLVM_LINK_COMPONENTS
  BitWriter
)
add_llvm_library(BCDBBitcode
  AlignBitcode.cpp
  BitstreamReader.cpp
)

set(LLVM_LINK_COMPONENTS
  Core
  Object
  Support
)
add_llvm_library(BCDBWholeProgram
  ImitateBinary.cpp
)
target_link_libraries(BCDBWholeProgram PRIVATE
  BCDBBitcode
)

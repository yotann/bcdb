set(LLVM_LINK_COMPONENTS
  LTO
  Support
  TransformUtils
)
add_llvm_library(BCDBSplit
  Split.cpp
)

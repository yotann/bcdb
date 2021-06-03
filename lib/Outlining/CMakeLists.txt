set(LLVM_LINK_COMPONENTS
  LTO
  Support
  TransformUtils
)

add_llvm_loadable_module(BCDBOutliningPlugin
  Candidates.cpp
  CostModel.cpp
  Dependence.cpp
  Extractor.cpp
  LinearProgram.cpp

  PLUGIN_TOOL
  opt
)
# The plugin can't use ASAN because the main opt executable was compiled
# without it.
target_compile_options(BCDBOutliningPlugin PRIVATE "-fno-sanitize=address")
target_link_options(BCDBOutliningPlugin PRIVATE "-fno-sanitize=address")

add_llvm_library(BCDBOutlining
  Candidates.cpp
  CostModel.cpp
  Dependence.cpp
  Extractor.cpp
  LinearProgram.cpp
)

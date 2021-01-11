set(LLVM_LINK_COMPONENTS
  LTO
  Support
  TransformUtils
)
add_llvm_loadable_module(BCDBOutliningPlugin
  Candidates.cpp
  Dependence.cpp
  Extractor.cpp

  PLUGIN_TOOL
  opt
)

set(LLVM_OPTIONAL_SOURCES Funcs.cpp OutliningPlugin.cpp)

add_llvm_library(BCDBOutliningPlugin MODULE
  Candidates.cpp
  Dependence.cpp
  Extractor.cpp
  LinearProgram.cpp
  OutliningPlugin.cpp
  SizeModel.cpp

  PLUGIN_TOOL
  opt
)
# The plugin can't use sanitizers because the main opt executable was compiled
# without them.
target_compile_options(BCDBOutliningPlugin PRIVATE "-fno-sanitize=address" "-fno-sanitize=undefined")
target_link_options(BCDBOutliningPlugin PRIVATE "-fno-sanitize=address" "-fno-sanitize=undefined")

add_llvm_library(BCDBOutlining
  Candidates.cpp
  Dependence.cpp
  Extractor.cpp
  Funcs.cpp
  LinearProgram.cpp
  SizeModel.cpp
)

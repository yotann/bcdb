add_custom_command(OUTPUT friendly_MemorySSA.h
  COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../../utils/make_friendly_memoryssa.py ${LLVM_INCLUDE_DIR}/llvm/Analysis/MemorySSA.h friendly_MemorySSA.h
  DEPENDS ${LLVM_INCLUDE_DIR}/llvm/Analysis/MemorySSA.h ${CMAKE_CURRENT_SOURCE_DIR}/../../utils/make_friendly_memoryssa.py
)
add_custom_target(friendly_memoryssa DEPENDS friendly_MemorySSA.h)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

set(LLVM_OPTIONAL_SOURCES Funcs.cpp OutliningPlugin.cpp)

add_llvm_library(BCDBOutliningPlugin MODULE
  Candidates.cpp
  Dependence.cpp
  Extractor.cpp
  FalseMemorySSA.cpp
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
add_dependencies(BCDBOutliningPlugin friendly_memoryssa)

add_llvm_library(liboutlining
  Candidates.cpp
  Dependence.cpp
  Extractor.cpp
  FalseMemorySSA.cpp
  Funcs.cpp
  LinearProgram.cpp
  SizeModel.cpp
)
target_link_libraries(liboutlining PRIVATE
  libbcdb
  libmemodb
)
add_dependencies(liboutlining friendly_memoryssa)

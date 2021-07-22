include_directories(${CMAKE_CURRENT_BINARY_DIR})

set(LLVM_LINK_COMPONENTS
  BitWriter
  LTO
  Support
  TransformUtils
)
add_llvm_library(libbcdb
  AlignBitcode.cpp
  BCDB.cpp
  BitstreamReader.cpp
  ImitateBinary.cpp
  Join.cpp
  Split.cpp
  Util.cpp
)
target_link_libraries(libbcdb PRIVATE
  libmemodb
)

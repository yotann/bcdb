add_custom_command(OUTPUT mux_main.bc
  COMMAND clang -emit-llvm -Os -c ${CMAKE_CURRENT_SOURCE_DIR}/data/mux_main.c -o mux_main.bc
  DEPENDS data/mux_main.c
)
add_custom_command(OUTPUT mux_main.inc
  COMMAND xxd -i mux_main.bc mux_main.inc
  DEPENDS mux_main.bc
)
add_custom_target(mux_main DEPENDS mux_main.inc)

add_custom_command(OUTPUT mux2_library.bc
  COMMAND clang -emit-llvm -Os -c ${CMAKE_CURRENT_SOURCE_DIR}/data/mux2_library.c -o mux2_library.bc
  DEPENDS data/mux2_library.c
)
add_custom_command(OUTPUT mux2_library.inc
  COMMAND xxd -i mux2_library.bc mux2_library.inc
  DEPENDS mux2_library.bc
)
add_custom_target(mux2_library DEPENDS mux2_library.inc)

include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_llvm_library(BCDB
  BCDB.cpp
  Merge.cpp
  Mux.cpp
  Mux2.cpp
  Util.cpp
)
target_link_libraries(BCDB PRIVATE
  BCDBBitcode
  BCDBSplit
  MemoDB
)
add_dependencies(BCDB mux_main)
add_dependencies(BCDB mux2_library)

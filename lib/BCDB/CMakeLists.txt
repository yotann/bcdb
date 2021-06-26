add_custom_command(OUTPUT mux_main.bc
  COMMAND ${CLANG} -emit-llvm -Os -c ${CMAKE_CURRENT_SOURCE_DIR}/data/mux_main.c -o mux_main.bc
  DEPENDS data/mux_main.c
)
add_custom_command(OUTPUT mux_main.inc
  COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../../utils/xxd.py mux_main.bc mux_main.inc
  DEPENDS mux_main.bc
)
add_custom_target(mux_main DEPENDS mux_main.inc)

add_custom_command(OUTPUT gl_library.bc
  COMMAND ${CLANG} -emit-llvm -Os -c ${CMAKE_CURRENT_SOURCE_DIR}/data/gl_library.c -o gl_library.bc
  DEPENDS data/gl_library.c
)
add_custom_command(OUTPUT gl_library.inc
  COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../../utils/xxd.py gl_library.bc gl_library.inc
  DEPENDS gl_library.bc
)
add_custom_target(gl_library DEPENDS gl_library.inc)

add_custom_command(OUTPUT gl_default_symbol_list.inc
  COMMAND cp ${CMAKE_CURRENT_SOURCE_DIR}/data/gl_default_symbol_list.txt gl_default_symbol_list.txt
  COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../../utils/xxd.py gl_default_symbol_list.txt gl_default_symbol_list.inc
  DEPENDS data/gl_default_symbol_list.txt)
add_custom_target(gl_default_symbol_list DEPENDS gl_default_symbol_list.inc)

include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_llvm_library(BCDB
  BCDB.cpp
  GuidedLinker.cpp
  Merge.cpp
  Mux.cpp
  Util.cpp
)
target_link_libraries(BCDB PRIVATE
  BCDBBitcode
  BCDBSplit
  MemoDB
)
add_dependencies(BCDB mux_main)
add_dependencies(BCDB gl_library)
add_dependencies(BCDB gl_default_symbol_list)

add_custom_command(OUTPUT repl_init.inc
  COMMAND cp ${CMAKE_CURRENT_SOURCE_DIR}/repl_init.js repl_init.js
  COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../../utils/xxd.py repl_init.js repl_init.inc
  DEPENDS repl_init.js)
add_custom_target(repl_init DEPENDS repl_init.inc)

include_directories(${CMAKE_CURRENT_BINARY_DIR})

set(LLVM_LINK_COMPONENTS
  Support
)
add_llvm_tool(memodb
  memodb.cpp
)
target_link_libraries(memodb PRIVATE
  Duktape
  Linenoise
  MemoDB
)
add_dependencies(memodb repl_init)

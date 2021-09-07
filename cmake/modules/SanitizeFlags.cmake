SET(CMAKE_CXX_FLAGS_SANITIZE
  "${CMAKE_CXX_FLAGS_DEBUG_INIT} -fsanitize=address -fsanitize=undefined"
  CACHE STRING "Flags used by the compiler during sanitizer builds."
  FORCE )
SET(CMAKE_C_FLAGS_SANITIZE
  "${CMAKE_C_FLAGS_DEBUG_INIT} -fsanitize=address -fsanitize=undefined"
  CACHE STRING "Flags used by the compiler during sanitizer builds."
  FORCE )
SET(CMAKE_EXE_LINKER_FLAGS_SANITIZE
  "${CMAKE_EXE_LINKER_FLAGS_DEBUG_INIT} -fsanitize=address -fsanitize=undefined"
  CACHE STRING "Flags used by the linker during sanitizer builds."
  FORCE )
SET(CMAKE_MODULE_LINKER_FLAGS_SANITIZE
  "${CMAKE_MODULE_LINKER_FLAGS_DEBUG_INIT} -fsanitize=address -fsanitize=undefined"
  CACHE STRING "Flags used by the linker during sanitizer builds."
  FORCE )
SET(CMAKE_SHARED_LINKER_FLAGS_SANITIZE
  "${CMAKE_SHARED_LINKER_FLAGS_DEBUG_INIT} -fsanitize=address -fsanitize=undefined"
  CACHE STRING "Flags used by the linker during sanitizer builds."
  FORCE )
MARK_AS_ADVANCED(
  CMAKE_CXX_FLAGS_SANITIZE
  CMAKE_C_FLAGS_SANITIZE
  CMAKE_EXE_LINKER_FLAGS_SANITIZE
  CMAKE_MODULE_LINKER_FLAGS_SANITIZE
  CMAKE_SHARED_LINKER_FLAGS_SANITIZE)

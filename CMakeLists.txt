cmake_minimum_required(VERSION 3.4)

project(BCDB C CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_definitions(-Wall)

SET(CMAKE_CXX_FLAGS_COVERAGE
  "${CMAKE_CXX_FLAGS_DEBUG_INIT} --coverage"
  CACHE STRING "Flags used by the compiler during coverage builds."
  FORCE )
SET(CMAKE_C_FLAGS_COVERAGE
  "${CMAKE_C_FLAGS_DEBUG_INIT} --coverage"
  CACHE STRING "Flags used by the compiler during coverage builds."
  FORCE )
SET(CMAKE_EXE_LINKER_FLAGS_COVERAGE
  "${CMAKE_EXE_LINKER_FLAGS_DEBUG_INIT} --coverage"
  CACHE STRING "Flags used by the linker during coverage builds."
  FORCE )
SET(CMAKE_MODULE_LINKER_FLAGS_COVERAGE
  "${CMAKE_MODULE_LINKER_FLAGS_DEBUG_INIT} --coverage"
  CACHE STRING "Flags used by the linker during coverage builds."
  FORCE )
SET(CMAKE_SHARED_LINKER_FLAGS_COVERAGE
  "${CMAKE_SHARED_LINKER_FLAGS_DEBUG_INIT} --coverage"
  CACHE STRING "Flags used by the linker during coverage builds."
  FORCE )
MARK_AS_ADVANCED(
  CMAKE_CXX_FLAGS_COVERAGE
  CMAKE_C_FLAGS_COVERAGE
  CMAKE_EXE_LINKER_FLAGS_COVERAGE
  CMAKE_MODULE_LINKER_FLAGS_COVERAGE
  CMAKE_SHARED_LINKER_FLAGS_COVERAGE)

find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
if( ${LLVM_PACKAGE_VERSION} VERSION_LESS 4.0 )
  message(FATAL_ERROR "LLVM 4.0 or newer is required")
endif()
if( ${LLVM_PACKAGE_VERSION} STREQUAL 10.0.0git )
  message(WARNING "Support for this version of LLVM is unstable")
elseif( NOT (${LLVM_PACKAGE_VERSION} VERSION_LESS 10.0) )
  message(FATAL_ERROR "LLVM 9 or older is required")
endif()
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

set(Python_ADDITIONAL_VERSIONS 2.7)
include(FindPythonInterp)
if( NOT PYTHONINTERP_FOUND )
  message(FATAL_ERROR
"Unable to find Python interpreter, required for builds and testing.
Please install Python or specify the PYTHON_EXECUTABLE CMake variable.")
endif()
if( ${PYTHON_VERSION_STRING} VERSION_LESS 2.7 )
  message(FATAL_ERROR "Python 2.7 or newer is required")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBSODIUM REQUIRED libsodium)
link_directories(${LIBSODIUM_LIBRARY_DIRS})
pkg_check_modules(SQLITE3 REQUIRED sqlite3)
link_directories(${SQLITE3_LIBRARY_DIRS})



set(LLVM_BUILD_TOOLS ON)
option(LLVM_ENABLE_ASSERTIONS "Enable assertions" ON)
option(LLVM_ENABLE_WARNINGS "Enable warnings" ON)

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)
include(HandleLLVMOptions)

# Put libs and bins in expected place.
# XXX: We don't setup CMAKE_CFG_INTDIR to distinguish Release/Debug/etc.
set(LLVM_RUNTIME_OUTPUT_INTDIR
    ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/bin)
set(LLVM_LIBRARY_OUTPUT_INTDIR
    ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/lib${LLVM_LIBDIR_SUFFIX})

link_directories(${LLVM_LIBRARY_DIR})
include_directories(${LLVM_INCLUDE_DIR})



# LLVM started using PRIVATE in version 6. We must use it when LLVM does for
# compatibility.
function(tool_link_libraries binary)
  if( ${LLVM_VERSION_MAJOR} GREATER 5 )
    target_link_libraries(${binary}
      PRIVATE
      ${ARGN}
    )
  else()
    target_link_libraries(${binary}
      ${ARGN}
    )
  endif()
endfunction()



include_directories(${PROJECT_SOURCE_DIR}/include)

add_subdirectory(lib)
add_subdirectory(test)
add_subdirectory(tools)

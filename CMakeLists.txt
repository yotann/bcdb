cmake_minimum_required(VERSION 3.4)

project(BCDB C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  add_definitions(-Wall
    -Wno-covered-switch-default)
elseif (CMAKE_COMPILER_IS_GNUCXX)
  add_definitions(-Wall
    -Wno-redundant-move # sometimes Clang requires a move that GCC warns about
    -Wno-init-list-lifetime)
endif()

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

SET(CMAKE_CXX_FLAGS_ASAN
  "${CMAKE_CXX_FLAGS_DEBUG_INIT} -fsanitize=address"
  CACHE STRING "Flags used by the compiler during address sanitizer builds."
  FORCE )
SET(CMAKE_C_FLAGS_ASAN
  "${CMAKE_C_FLAGS_DEBUG_INIT} -fsanitize=address"
  CACHE STRING "Flags used by the compiler during address sanitizer builds."
  FORCE )
SET(CMAKE_EXE_LINKER_FLAGS_ASAN
  "${CMAKE_EXE_LINKER_FLAGS_DEBUG_INIT} -fsanitize=address"
  CACHE STRING "Flags used by the linker during address sanitizer builds."
  FORCE )
SET(CMAKE_MODULE_LINKER_FLAGS_ASAN
  "${CMAKE_MODULE_LINKER_FLAGS_DEBUG_INIT} -fsanitize=address"
  CACHE STRING "Flags used by the linker during address sanitizer builds."
  FORCE )
SET(CMAKE_SHARED_LINKER_FLAGS_ASAN
  "${CMAKE_SHARED_LINKER_FLAGS_DEBUG_INIT} -fsanitize=address"
  CACHE STRING "Flags used by the linker during address sanitizer builds."
  FORCE )
MARK_AS_ADVANCED(
  CMAKE_CXX_FLAGS_ASAN
  CMAKE_C_FLAGS_ASAN
  CMAKE_EXE_LINKER_FLAGS_ASAN
  CMAKE_MODULE_LINKER_FLAGS_ASAN
  CMAKE_SHARED_LINKER_FLAGS_ASAN)

find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
if( ${LLVM_PACKAGE_VERSION} VERSION_LESS 7.0 )
  message(FATAL_ERROR "LLVM 7.0 or newer is required")
endif()
if( ${LLVM_PACKAGE_VERSION} STREQUAL 13.0.0git )
  message(WARNING "Support for this version of LLVM is unstable")
elseif( NOT (${LLVM_PACKAGE_VERSION} VERSION_LESS 13.0) )
  message(FATAL_ERROR "LLVM 12 or older is required")
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
find_program(CLANG clang)
find_program(XXD xxd)

find_package(Threads) # needed by nng
find_package(nng)
find_package(nngpp)
if(Threads_FOUND AND nng_FOUND AND nngpp_FOUND)
  set(WITH_NNG ON)
  set(ENABLE_SMOUT ON)
else(Threads_FOUND AND nng_FOUND AND nngpp_FOUND)
  message(WARNING "Could not find nng and nngpp libraries; disabling smout.")
  set(WITH_NNG OFF)
  set(ENABLE_SMOUT OFF)
endif(Threads_FOUND AND nng_FOUND AND nngpp_FOUND)

find_library(LEVELDB_LIBRARIES leveldb)
find_path(LEVELDB_INCLUDE_DIRS leveldb/db.h)
if(LEVELDB_LIBRARIES AND LEVELDB_INCLUDE_DIRS)
  set(WITH_LEVELDB ON)
else(LEVELDB_LIBRARIES AND LEVELDB_INCLUDE_DIRS)
  message(WARNING "Could not find LevelDB library.")
  set(WITH_LEVELDB OFF)
endif(LEVELDB_LIBRARIES AND LEVELDB_INCLUDE_DIRS)



set(LLVM_BUILD_TOOLS ON)
option(LLVM_ENABLE_ASSERTIONS "Enable assertions" ON)
option(LLVM_ENABLE_WARNINGS "Enable warnings" ON)

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)
include(HandleLLVMOptions)
set(LLVM_REQUIRES_EH ON) # exceptions needed by nngpp

# Put libs and bins in expected place.
# XXX: We don't setup CMAKE_CFG_INTDIR to distinguish Release/Debug/etc.
set(LLVM_RUNTIME_OUTPUT_INTDIR
    ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/bin)
set(LLVM_LIBRARY_OUTPUT_INTDIR
    ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/lib${LLVM_LIBDIR_SUFFIX})
if(WIN32 OR CYGWIN)
  set(LLVM_SHLIB_OUTPUT_INTDIR ${LLVM_RUNTIME_OUTPUT_INTDIR})
else()
  set(LLVM_SHLIB_OUTPUT_INTDIR ${LLVM_LIBRARY_OUTPUT_INTDIR})
endif()

link_directories(${LLVM_LIBRARY_DIR})
include_directories(${LLVM_INCLUDE_DIR})



# LLVM stopped using add_llvm_loadable_module in version 8.
if( ${LLVM_VERSION_MAJOR} GREATER 7 )
  function(add_llvm_loadable_module name)
    add_llvm_library(${name} MODULE ${ARGN})
  endfunction()
endif()



include_directories(${PROJECT_SOURCE_DIR}/include)

set(GOOGLETEST_VERSION 1.10.0)
add_subdirectory(third_party/googletest)

add_subdirectory(lib)
add_subdirectory(test)
add_subdirectory(tools)
add_subdirectory(unittests)

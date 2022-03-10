cmake_minimum_required(VERSION 3.13)

project(BCDB
  VERSION 0.1.0
  HOMEPAGE_URL "https://github.com/yotann/bcdb-private"
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_MODULE_PATH
  ${CMAKE_MODULE_PATH}
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules"
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cmake-modules"
  )

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  add_definitions(-Wall
    -Wno-covered-switch-default)
elseif (CMAKE_COMPILER_IS_GNUCXX)
  add_definitions(-Wall
    -Wno-redundant-move # sometimes Clang requires a move that GCC warns about
    -Wno-init-list-lifetime)
endif()

include(CoverageFlags)
include(SanitizeFlags)
include(GetRevisionDescription)
get_revision_description(REVISION_DESCRIPTION_FINAL)

find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
if( ${LLVM_PACKAGE_VERSION} VERSION_LESS 11.0 )
  message(FATAL_ERROR "LLVM 11 or newer is required")
endif()
if( ${LLVM_PACKAGE_VERSION} STREQUAL 14.0.0git )
  message(WARNING "Support for this version of LLVM is unstable")
elseif( NOT (${LLVM_PACKAGE_VERSION} VERSION_LESS 14.0) )
  message(FATAL_ERROR "LLVM 13 or older is required")
endif()
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

if(NOT LLVM_ENABLE_EH)
	message(FATAL_ERROR "LLVM must be built with exception handling enabled (LLVM_ENABLE_EH=ON). You may need to build it manually (or use Nix).")
endif()

include(FindPythonInterp)
if( NOT PYTHONINTERP_FOUND )
  message(FATAL_ERROR
"Unable to find Python interpreter, required for builds and testing.
Please install Python or specify the PYTHON_EXECUTABLE CMake variable.")
endif()
if( ${PYTHON_VERSION_STRING} VERSION_LESS 3.6 )
  message(FATAL_ERROR "Python 3.6 or newer is required")
endif()

find_package(LibSodium REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(Boost 1.75 REQUIRED COMPONENTS fiber)

add_library(Boost::Beast INTERFACE IMPORTED)
target_include_directories(Boost::Beast SYSTEM INTERFACE ${Boost_INCLUDE_DIRS})
target_compile_definitions(Boost::Beast INTERFACE BOOST_BEAST_USE_STD_STRING_VIEW)

find_program(CLANG clang)
find_program(CLANGXX clang++)
if(NOT CLANG OR NOT CLANGXX)
  message(FATAL_ERROR "Clang is required, but missing")
endif()

set(ENABLE_SMOUT ON)

find_package(RocksDB QUIET)
if(RocksDB_FOUND)
  set(WITH_ROCKSDB ON)
else(RocksDB_FOUND)
  message(WARNING "Could not find RocksDB library; disabling RocksDB support.")
  set(WITH_ROCKSDB OFF)
endif(RocksDB_FOUND)



set(LLVM_BUILD_TOOLS ON)
option(LLVM_ENABLE_ASSERTIONS "Enable assertions" ON)
option(LLVM_ENABLE_WARNINGS "Enable warnings" ON)

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)
include(HandleLLVMOptions)

# Put libs and bins in expected place.
# XXX: We don't setup CMAKE_CFG_INTDIR to distinguish Release/Debug/etc.
set(LLVM_TOOLS_INSTALL_DIR
  ${CMAKE_INSTALL_BINDIR})
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



include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories("${PROJECT_SOURCE_DIR}/third_party/dragonbox/include")

add_subdirectory(lib)
add_subdirectory(test)
add_subdirectory(third_party)
add_subdirectory(tools)
add_subdirectory(unittests)

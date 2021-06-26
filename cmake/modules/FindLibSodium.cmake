# Look for the necessary header
find_path(LibSodium_INCLUDE_DIR NAMES sodium.h)
mark_as_advanced(LibSodium_INCLUDE_DIR)

# Look for the necessary library
find_library(LibSodium_LIBRARY NAMES sodium)
mark_as_advanced(LibSodium_LIBRARY)

# Extract version information from the header file
if(LibSodium_INCLUDE_DIR)
  file(STRINGS ${LibSodium_INCLUDE_DIR}/sodium/version.h _ver_line
    REGEX "^#define SODIUM_VERSION_STRING  *\"[0-9]+\\.[0-9]+\\.[0-9]+\""
         LIMIT_COUNT 1)
    string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+"
      LibSodium_VERSION "${_ver_line}")
    unset(_ver_line)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibSodium
  REQUIRED_VARS LibSodium_INCLUDE_DIR LibSodium_LIBRARY
  VERSION_VAR LibSodium_VERSION)

# Create the imported target
if(LibSodium_FOUND)
  set(LibSodium_INCLUDE_DIRS ${LibSodium_INCLUDE_DIR})
  set(LibSodium_LIBRARIES ${LibSodium_LIBRARY})
  if(NOT TARGET Sodium::LibSodium)
    add_library(Sodium::LibSodium UNKNOWN IMPORTED)
    set_target_properties(Sodium::LibSodium PROPERTIES
      IMPORTED_LOCATION             "${LibSodium_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${LibSodium_INCLUDE_DIR}")
    endif()
endif()

add_custom_target(UnitTests)

function(add_llvm_unittest test_dirname)
  add_unittest(UnitTests ${test_dirname} ${ARGN})

  # include/llvm/Testing/Support/Error.h includes gmock.h,
  # but we don't need to link to the gmock library.
  target_include_directories(${test_dirname} PRIVATE
    ${PROJECT_SOURCE_DIR}/third_party/googletest/googlemock/include)
endfunction()

add_subdirectory(memodb)
add_subdirectory(outlining)

function(add_llvm_unittest test_dirname)
  add_unittest(UnitTests ${test_dirname} ${ARGN})
  target_link_libraries(${test_dirname} PUBLIC gmock)
endfunction()

add_subdirectory(outlining)

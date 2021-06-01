add_custom_target(UnitTests)

function(add_llvm_unittest test_dirname)
  add_unittest(UnitTests ${test_dirname} ${ARGN})
endfunction()

add_subdirectory(MemoDB)
if( ${LLVM_VERSION_MAJOR} GREATER 8 )
add_subdirectory(Outlining)
endif()

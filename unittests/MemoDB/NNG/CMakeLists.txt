if(WITH_NNG)
  add_llvm_unittest(NNGTests
    NNGTest.cpp
  )

  target_link_libraries(NNGTests PRIVATE
    MemoDB
    nng::nng
  )
endif(WITH_NNG)

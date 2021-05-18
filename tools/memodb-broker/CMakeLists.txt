if(WITH_NNG)
  set(LLVM_LINK_COMPONENTS
    Support
  )
  add_llvm_tool(memodb-broker
    memodb-broker.cpp
  )
  target_link_libraries(memodb-broker PRIVATE
    MemoDB
    nng::nng
    nng::nngpp
  )
endif(WITH_NNG)

if(WITH_NNG)
  set(LLVM_LINK_COMPONENTS
    Support
  )
  add_llvm_tool(memodb-broker
    memodb-broker.cpp
  )
  target_link_libraries(memodb-broker PRIVATE
    libmemodb
    nng::nng
  )
endif(WITH_NNG)

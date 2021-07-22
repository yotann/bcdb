if(WITH_NNG)
  set(LLVM_LINK_COMPONENTS
    Support
  )
  add_llvm_tool(memodb-server
    memodb-server.cpp
  )
  target_link_libraries(memodb-server PRIVATE
    libmemodb
    nng::nng
  )
endif(WITH_NNG)

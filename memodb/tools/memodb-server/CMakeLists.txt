set(LLVM_LINK_COMPONENTS
	Support
)
add_llvm_tool(memodb-server
	memodb-server.cpp
)
target_link_libraries(memodb-server PRIVATE
	libmemodb
	Boost::Beast
)

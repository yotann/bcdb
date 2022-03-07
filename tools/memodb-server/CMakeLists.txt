set(LLVM_LINK_COMPONENTS
	Support
)
add_llvm_tool(memodb-server
	memodb-server.cpp
)
target_link_libraries(memodb-server PRIVATE
	libmemodb
)
target_include_directories(memodb-server PRIVATE
	${Boost_INCLUDE_DIRS}
)

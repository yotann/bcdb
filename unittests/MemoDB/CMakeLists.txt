add_llvm_unittest(MemoDBTests
  CborLoadTest.cpp
  CborSaveTest.cpp
)

tool_link_libraries(MemoDBTests
  MemoDB
)

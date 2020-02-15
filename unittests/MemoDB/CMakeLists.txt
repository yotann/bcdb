add_llvm_unittest(MemoDBTests
  CborSaveTest.cpp
)

tool_link_libraries(MemoDBTests
  MemoDB
)

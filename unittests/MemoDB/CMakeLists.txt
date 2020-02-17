add_llvm_unittest(MemoDBTests
  CborLoadTest.cpp
  CborSaveTest.cpp
  ValuePrintTest.cpp
)

tool_link_libraries(MemoDBTests
  MemoDB
)

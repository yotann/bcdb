add_llvm_unittest(MemoDBTests
  CborLoadTest.cpp
  CborSaveTest.cpp
  CIDTest.cpp
  ParsedURITest.cpp
  ValuePrintTest.cpp
)

target_link_libraries(MemoDBTests PRIVATE
  MemoDB
)

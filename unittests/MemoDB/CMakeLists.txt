add_llvm_unittest(MemoDBTests
  CborLoadTest.cpp
  CborSaveTest.cpp
  CIDTest.cpp
  JSONWriteTest.cpp
  ParsedURITest.cpp
)

target_link_libraries(MemoDBTests PRIVATE
  MemoDB
)

add_llvm_unittest(MemoDBTests
  CborLoadTest.cpp
  CborSaveTest.cpp
  CIDTest.cpp
  EvaluatorTest.cpp
  JSONLoadTest.cpp
  JSONWriteTest.cpp
  URITest.cpp
)

target_link_libraries(MemoDBTests PRIVATE
  MemoDB
)

add_subdirectory(NNG)

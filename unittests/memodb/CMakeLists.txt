add_llvm_unittest(MemoDBTests
  CborLoadTest.cpp
  CborSaveTest.cpp
  CIDTest.cpp
  EvaluatorTest.cpp
  HTTPTest.cpp
  JSONLoadTest.cpp
  JSONWriteTest.cpp
  ServerTest.cpp
  URITest.cpp
)

target_link_libraries(MemoDBTests PRIVATE
  libmemodb
)

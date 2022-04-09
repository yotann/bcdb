add_unittest(UnitTests MemoDBTests
  CborLoadTest.cpp
  CborSaveTest.cpp
  CIDTest.cpp
  EvaluatorTest.cpp
  HTTPTest.cpp
  JSONLoadTest.cpp
  JSONWriteTest.cpp
  MultibaseTest.cpp
  RequestTest.cpp
  ServerTest.cpp
  URITest.cpp
)

target_link_libraries(MemoDBTests PRIVATE
  gmock
  libmemodb
)

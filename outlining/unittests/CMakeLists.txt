add_unittest(UnitTests OutliningTests
  LinearProgramTest.cpp
)

target_link_libraries(OutliningTests PRIVATE
  gmock
  liboutlining
)

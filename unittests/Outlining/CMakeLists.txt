add_llvm_unittest(OutliningTests
  LinearProgramTest.cpp
)

target_link_libraries(OutliningTests PRIVATE
  BCDBOutlining
)

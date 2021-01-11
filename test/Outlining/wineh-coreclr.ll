; RUN: opt -load %shlibdir/BCDBOutliningPlugin%shlibext \
; RUN:     -outlining-extractor -outline-unprofitable -verify -S %s

; based on llvm/test/CodeGen/X86/wineh-coreclr.ll

declare void @ProcessCLRException()
declare void @f(i32)

define void @test2() personality i8* bitcast (void ()* @ProcessCLRException to i8*) {
entry:
  invoke void @f(i32 1)
    to label %exit unwind label %fault
fault:
  %fault.pad = cleanuppad within none [i32 undef]
  invoke void @f(i32 2) ["funclet"(token %fault.pad)]
    to label %unreachable unwind label %exn.dispatch.inner
exn.dispatch.inner:
  %catchswitch.inner = catchswitch within %fault.pad [label %catch1] unwind label %exn.dispatch.outer
catch1:
  %catch.pad1 = catchpad within %catchswitch.inner [i32 1]
  catchret from %catch.pad1 to label %unreachable
exn.dispatch.outer:
  %catchswitch.outer = catchswitch within none [label %catch2] unwind to caller
catch2:
  %catch.pad2 = catchpad within %catchswitch.outer [i32 2]
  catchret from %catch.pad2 to label %exit
exit:
  ret void
unreachable:
  unreachable
}

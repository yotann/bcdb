; RUN: opt -load %shlibdir/BCDBOutliningPlugin%shlibext \
; RUN:     -outlining-dependence -analyze %s | FileCheck %s


; "@may_throw_or_exit" might throw an exception or exit the program, in which
; case the side effects of @may_have_side_effects and the sdiv (divide by 0)
; will be skipped. That means it would be illegal to outline just "%x = ..."
; and "@may_have_side_effects", or just "%x = ..." and "%y = ...", because
; then the side effects would happen regardless of @may_throw_or_exit's
; behavior.



; CHECK-LABEL: define void @singleblock
define void @singleblock(i32 %i) {
; CHECK: entry:
; CHECK-NEXT: block 0
entry:
; CHECK-NEXT: node 1 arg [0] dominating [0]
; CHECK-NEXT: %x = add i32 %i, %i
  %x = add i32 %i, %i
; CHECK-NEXT: node 2 dominating [0]
; CHECK-NEXT: call void @may_throw_or_exit()
  call void @may_throw_or_exit()
; CHECK-NEXT: node 3 data [1] dominating [0-2]
; CHECK-NEXT: call void @may_have_side_effects(i32 %x)
  call void @may_have_side_effects(i32 %x)
; CHECK-NEXT: node 4 data [1] dominating [0-3]
; CHECK-NEXT: %y = sdiv i32 1, %x
  %y = sdiv i32 1, %x
  ret void
}



; CHECK-LABEL: define void @manyblocks
define void @manyblocks(i32 %i) {
; CHECK: entry:
; CHECK-NEXT: block 0
entry:
; CHECK-NEXT: node 1 arg [0] dominating [0]
; CHECK-NEXT: %x = add i32 %i, %i
  %x = add i32 %i, %i
; CHECK-NEXT: node 2 dominating [0]
; CHECK-NEXT: br label %b1
  br label %b1

; CHECK: b1:
; CHECK-NEXT: block 3
b1:
; CHECK-NEXT: node 4 dominating [3]
; CHECK-NEXT: call void @may_throw_or_exit()
  call void @may_throw_or_exit()
; CHECK-NEXT: node 5 dominating [3-4]
; CHECK-NEXT: br label %b2
  br label %b2

; CHECK: b2:
; CHECK-NEXT: block 6 dominating [3-5]
b2:
; CHECK-NEXT: node 7 data [1] dominating [0-1, 3-6]
; CHECK-NEXT: call void @may_have_side_effects(i32 %x)
  call void @may_have_side_effects(i32 %x)
; CHECK-NEXT: node 8 dominating [0-1, 3-7]
; CHECK-NEXT: br label %b3
  br label %b3

; CHECK: b3:
; CHECK-NEXT: block 9 dominating [0-1, 3-8]
b3:
; CHECK-NEXT: node 10 data [1] dominating [0-1, 3-9]
; CHECK-NEXT: %y = sdiv i32 1, %x
  %y = sdiv i32 1, %x
; CHECK-NEXT: node 11 dominating [0-1, 3-9]
; CHECK-NEXT: br label %b4
  br label %b4

; CHECK: b4:
; CHECK-NEXT: block 12 dominating [0-1, 3-9]
b4:
; CHECK-NEXT: node 13 prevents outlining
; CHECK-NEXT: ret void
  ret void
}



; LLVM Language Reference Manual for LLVM 13: "This means while [a readnone
; function] cannot unwind exceptions by calling the C++ exception throwing
; methods (since they write to memory), there may be non-C++ mechanisms that
; throw exceptions without writing to LLVM visible memory."
declare void @may_throw_or_exit() readnone
declare void @may_have_side_effects(i32)

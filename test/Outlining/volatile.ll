; RUN: opt -load %shlibdir/BCDBOutliningPlugin%shlibext \
; RUN:     -outlining-dependence -analyze %s | FileCheck %s

; RUN: opt -load %shlibdir/BCDBOutliningPlugin%shlibext \
; RUN:     -outlining-extractor -outline-unprofitable -verify -S %s

; based on llvm/test/Analysis/MemorySSA/volatile-clobber.ll

; CHECK-LABEL: define i32 @foo
define i32 @foo() {
  %1 = alloca i32, align 4
; CHECK: node 2 depends [0, 1] forced []
; CHECK-NEXT: store volatile i32 4
  store volatile i32 4, i32* %1, align 4
; CHECK: node 3 depends [0, 1, 2] forced []
; CHECK-NEXT: store volatile i32 8
  store volatile i32 8, i32* %1, align 4
; CHECK: node 4 depends [0, 1, 2, 3] forced []
; CHECK-NEXT: load volatile i32
  %2 = load volatile i32, i32* %1, align 4
; CHECK: node 5 depends [0, 1, 2, 3, 4] forced []
; CHECK-NEXT: load volatile i32
  %3 = load volatile i32, i32* %1, align 4
  %4 = add i32 %3, %2
  ret i32 %4
}

; CHECK-LABEL: define void @volatile_only
define void @volatile_only(i32* %arg1, i32* %arg2) {
  %a = alloca i32
  %b = alloca i32

; CHECK: node 3 depends [0, 1] forced []
; CHECK-NEXT: load volatile i32, i32* %a
  load volatile i32, i32* %a
; CHECK: node 4 depends [0, 2] forced []
; CHECK-NEXT: load i32, i32* %b
  load i32, i32* %b
; CHECK: node 5 depends [0, 1] forced []
; CHECK-NEXT: load i32, i32* %a
  load i32, i32* %a

; CHECK: node 6 depends [0, 1, 3] forced []
; CHECK-NEXT: load volatile i32, i32* %arg1
  load volatile i32, i32* %arg1

; CHECK: node 7 depends [0] forced []
; CHECK-NEXT: load i32, i32* %arg2
  load i32, i32* %arg2

  ret void
}

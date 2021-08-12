; RUN: opt -load %shlibdir/BCDBOutliningPlugin%shlibext \
; RUN:     -outlining-dependence -force-transitive-closures -analyze %s \
; RUN: | FileCheck %s

; RUN: %outliningtest --no-run %s

; based on llvm/test/Analysis/MemorySSA/volatile-clobber.ll

; CHECK-LABEL: define i32 @foo
define i32 @foo() {
  %1 = alloca i32, align 4
; CHECK: node 2 data [1] dominating [0-1]
; CHECK-NEXT: store volatile i32 4
  store volatile i32 4, i32* %1, align 4
; CHECK: node 3 data [1] dominating [0-2]
; CHECK-NEXT: store volatile i32 8
  store volatile i32 8, i32* %1, align 4
; CHECK: node 4 data [1] dominating [0-3]
; CHECK-NEXT: load volatile i32
  %2 = load volatile i32, i32* %1, align 4
; CHECK: node 5 data [1] dominating [0-4]
; CHECK-NEXT: load volatile i32
  %3 = load volatile i32, i32* %1, align 4
; CHECK: node 6 data [4-5] dominating [0-5]
; CHECK-NEXT: %4 = add i32 %3, %2
  %4 = add i32 %3, %2
  ret i32 %4
}

; CHECK-LABEL: define void @volatile_only
define void @volatile_only(i32* %arg1, i32* %arg2) {
; CHECK: node 1 prevents outlining
; CHECK-NEXT: %a = alloca i32
  %a = alloca i32
; CHECK: node 2 dominating [0] forced [2, 4]
; CHECK-NEXT: %b = alloca i32
  %b = alloca i32

; CHECK: node 3 data [1] dominating [0-1]
; CHECK-NEXT: load volatile i32, i32* %a
  load volatile i32, i32* %a
; CHECK: node 4 data [2] dominating [0, 2]
; CHECK-NEXT: load i32, i32* %b
  load i32, i32* %b
; CHECK: node 5 data [1] dominating [0-1]
; CHECK-NEXT: load i32, i32* %a
  load i32, i32* %a

; CHECK: node 6 arg [0] dominating [0-5]
; CHECK-NEXT: load volatile i32, i32* %arg1
  load volatile i32, i32* %arg1

; CHECK: node 7 arg [1] dominating [0]
; CHECK-NEXT: load i32, i32* %arg2
  load i32, i32* %arg2

  ret void
}

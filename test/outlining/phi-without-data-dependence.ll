; RUN: opt -load %shlibdir/BCDBOutliningPlugin%shlibext \
; RUN:     -outlining-dependence -analyze %s | FileCheck %s

; RUN: %outliningtest --no-run %s

; CHECK-LABEL: define i32 @f
define i32 @f(i1 %cond) {
entry:
; CHECK: block 0
; CHECK-NEXT: node 1 arg [0] dominating [0] forced [1-3]
  br i1 %cond, label %if, label %then

if:
; CHECK: block 2 dominating [0] forced [1-3]
; CHECK-NEXT: node 3 dominating [0-2]
  br label %then

then:
; CHECK: block 4 forced [1-3]
; CHECK-NEXT: node 5 dominating [0] forced [1-4]
; CHECK-NEXT: %x = phi
  %x = phi i32 [ 0, %entry ], [ 1, %if ]
  ret i32 %x
}

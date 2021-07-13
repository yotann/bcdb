; RUN: opt -load %shlibdir/BCDBOutliningPlugin%shlibext \
; RUN:     -outlining-dependence -analyze %s | FileCheck %s

; RUN: opt -load %shlibdir/BCDBOutliningPlugin%shlibext \
; RUN:     -outlining-extractor -outline-unprofitable -verify -S %s

; CHECK-LABEL: define i32 @f
define i32 @f(i1 %cond) {
entry:
; CHECK: block 0 depends [] forced []
; CHECK-NEXT: node 1 depends [0] forced [1, 2, 3]
  br i1 %cond, label %if, label %then

if:
; CHECK: block 2 depends [0, 1] forced []
; CHECK-NEXT: node 3 depends [0, 1, 2] forced []
  br label %then

then:
; CHECK: block 4 depends [] forced []
; CHECK-NEXT: node 5 depends [0, 1, 4] forced [1, 2, 3, 4]
; CHECK-NEXT: %x = phi
  %x = phi i32 [ 0, %entry ], [ 1, %if ]
  ret i32 %x
}

; RUN: opt -load %shlibdir/BCDBOutliningPlugin%shlibext \
; RUN:     -outlining-dependence -analyze %s | FileCheck %s

; RUN: opt -load %shlibdir/BCDBOutliningPlugin%shlibext \
; RUN:     -outlining-extractor -outline-unprofitable -verify -S %s

; CHECK-LABEL: define i32 @f
define i32 @f(i1 %cond) {
entry:
  br i1 %cond, label %if, label %then

if:
  br label %then

then:
; CHECK: node 5 depends [4] forced [1, 2, 3, 4]
; CHECK-NEXT: %x = phi
  %x = phi i32 [ 0, %entry ], [ 1, %if ]
  ret i32 %x
}

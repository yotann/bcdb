; RUN: opt -load %shlibdir/BCDBOutliningPlugin%shlibext \
; RUN:     -outline-only=1,2 -outlining-extractor -verify -S %s \
; RUN: | FileCheck %s

; RUN: opt -load %shlibdir/BCDBOutliningPlugin%shlibext \
; RUN:     -outlining-extractor -outline-unprofitable -verify -S %s

define i32 @f(i1 %cond, i32 %arg) {
entry:
; node 1
  %result = add i32 %arg, 1
; node 2
  br i1 %cond, label %true, label %false

true:
  ret i32 %result

false:
  ret i32 %result
}

; CHECK-LABEL: define { i32 } @f.outlined.1-2.callee(i1 %cond, i32 %arg) {
; CHECK: outline_return:
; CHECK-NEXT: phi i32 [ %result, %entry ], [ %result, %entry ]
; CHECK: entry:
; CHECK-NEXT: %result = add i32 %arg, 1
; CHECK-NEXT: br i1 %cond, label %outline_return, label %outline_return

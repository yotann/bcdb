; RUN: opt -load %shlibdir/BCDBOutliningPlugin%shlibext \
; RUN:     -outline-only=1,2,3,4,5 -outlining-extractor -verify -S %s \
; RUN: | FileCheck %s

; RUN: not --crash opt -load %shlibdir/BCDBOutliningPlugin%shlibext \
; RUN:     -outline-only=1,4,5 -outlining-extractor -verify -S %s

; RUN: opt -load %shlibdir/BCDBOutliningPlugin%shlibext \
; RUN:     -outlining-extractor -outline-unprofitable -verify -S %s

define i32 @f(i1 %cond, i32 %arg) {
; block 0 depends [] forced []
; node 1 depends [0] forced []
  br i1 %cond, label %true, label %false

true:
; block 2 depends [0, 1] forced []
; node 3 depends [0, 1, 2] forced []
  ret i32 %arg

false:
; block 4 depends [0, 1] forced []
; node 5 depends [0, 1, 4] forced []
  ret i32 0
}

; CHECK-LABEL: define { i32 } @f.outlined.1-5.callee(i1 %cond, i32 %arg) {
; CHECK: outline_entry:
; CHECK: br label %2
; CHECK: outline_return:
; CHECK: %0 = phi i32 [ %arg, %true ], [ 0, %false ]
; CHECK: %1 = insertvalue { i32 } undef, i32 %0, 0
; CHECK: ret { i32 } %1
; CHECK: 2:
; CHECK: br i1 %cond, label %true, label %false
; CHECK: true:
; CHECK: br label %outline_return
; CHECK: false:
; CHECK: br label %outline_return

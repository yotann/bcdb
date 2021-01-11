; RUN: opt -load %shlibdir/BCDBOutliningPlugin%shlibext \
; RUN:     -outline-only=1,2,3,4,5 -outlining-extractor -verify -S %s \
; RUN: | FileCheck %s

; RUN: opt -load %shlibdir/BCDBOutliningPlugin%shlibext \
; RUN:     -outline-only=1,4,5 -outlining-extractor -verify -S %s \
; RUN: | FileCheck --check-prefix=PARTIAL %s

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

; CHECK-LABEL: define { i32 } @f.outlined.1-5(i1 %cond, i32 %arg) {
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

; PARTIAL-LABEL: define { i32 } @f.outlined.1.4-5(i1 %cond) {
; PARTIAL: outline_entry:
; PARTIAL: br label %2
; PARTIAL: outline_return:
; PARTIAL: %0 = phi i32 [ 0, %false ], [ undef, %2 ]
; PARTIAL: %1 = insertvalue { i32 } undef, i32 %0, 0
; PARTIAL: ret { i32 } %1
; PARTIAL: 2:
; PARTIAL: br i1 %cond, label %outline_return, label %false
; PARTIAL: false:
; PARTIAL: br label %outline_return

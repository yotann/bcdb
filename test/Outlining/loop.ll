; RUN: opt -load %shlibdir/BCDBOutliningPlugin%shlibext \
; RUN:     -outline-only=3 -outlining-extractor -verify -S %s | FileCheck %s

; RUN: opt -load %shlibdir/BCDBOutliningPlugin%shlibext \
; RUN:     -outlining-extractor -outline-unprofitable -verify -S %s

define i32 @f(i32 %arg) {
; block 0 depends [] forced []
; node 1 depends [0] forced []
  br label %loopentry

loopentry:                                        ; preds = %loopbody, %0
; block 2 depends [] forced [2, 4]
; node 3 depends [2] forced []
  %x = add i32 %arg, 1
; node 4 depends [2] forced []
  br i1 undef, label %loopbody, label %exit

loopbody:                                         ; preds = %loopentry
; block 5 depends [2, 4] forced []
; node 6 depends [2, 4, 5] forced []
  br label %loopentry

exit:                                             ; preds = %loopentry
; block 7 depends [] forced []
; node 8 depends [2, 3, 7] forced []
  ret i32 %x
}

; CHECK-LABEL: define { i32 } @f.outlined.3.callee(i32 %arg) {
; CHECK: outline_entry:
; CHECK-NEXT: br label %loopentry
; CHECK: loopentry:
; CHECK-NEXT: %x = add i32 %arg, 1
; CHECK-NEXT: br label %outline_return

; CHECK-LABEL: define i32 @f.outlined.3.caller(i32 %arg) {
; CHECK: loopentry:
; CHECK-NEXT: %1 = call { i32 } @f.outlined.3.callee(i32 %arg)
; CHECK-NEXT: %x = extractvalue { i32 } %1, 0
; CHECK: exit:
; CHECK-NEXT: ret i32 %x

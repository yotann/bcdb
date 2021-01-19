; RUN: opt -load %shlibdir/BCDBOutliningPlugin%shlibext \
; RUN:     -outline-only=1 -outlining-extractor -verify -S %s | FileCheck %s

define void @f() {
; block 0 depends [] forced []
; node 1 depends [0] forced []
  ret void
}

; CHECK-LABEL: define {} @f.outlined.1.callee() {
; CHECK: outline_entry:
; CHECK: br label %0
; CHECK: outline_return:
; CHECK: ret {} undef
; CHECK: 0:
; CHECK: br label %outline_return

; CHECK-LABEL: define void @f.outlined.1.caller() {
; CHECK: %1 = call {} @f.outlined.1.callee()
; CHECK: ret void

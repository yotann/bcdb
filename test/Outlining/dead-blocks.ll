; RUN: opt -load %shlibdir/BCDBOutliningPlugin%shlibext \
; RUN:     -outlining-dependence -analyze %s | FileCheck %s

; RUN: opt -load %shlibdir/BCDBOutliningPlugin%shlibext \
; RUN:     -outline-only=1 -outlining-extractor -verify -S %s \
; RUN: | FileCheck --check-prefix=EXTRACT %s

; CHECK-LABEL: define void @f()
define void @f() {
; CHECK: block 0 depends [] forced []
; CHECK-NEXT: node 1 depends [0] forced []
; CHECK-NEXT: ret void
  ret void

; CHECK-NOT: block 2
; CHECK-NOT: node 2
dead0:
  ret void

dead1:
  br i1 undef, label %dead0, label %dead1
}

; EXTRACT-LABEL: define {} @f.outlined.1.callee() {
; EXTRACT: outline_entry:
; EXTRACT: br label %0
; EXTRACT: outline_return:
; EXTRACT: ret {} undef
; EXTRACT: 0:
; EXTRACT: br label %outline_return

; EXTRACT-LABEL: define void @f.outlined.1.caller() {
; EXTRACT-NEXT: %1 = call {} @f.outlined.1.callee()
; EXTRACT-NEXT: ret void

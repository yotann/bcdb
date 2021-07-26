; RUN: opt -load %shlibdir/BCDBOutliningPlugin%shlibext \
; RUN:     -outlining-dependence -analyze %s | FileCheck %s --match-full-lines

; RUN: %outliningtest --no-run %s

; CHECK-LABEL: define void @f() {
define void @f() {
; CHECK-NEXT: ; block 0
; CHECK-NEXT: ; node 1 prevents outlining
; CHECK-NEXT: ret void
  ret void
}
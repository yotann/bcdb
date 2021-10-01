; RUN: %outopt -outlining-dependence -force-transitive-closures -analyze %s \
; RUN: | FileCheck %s --match-full-lines

; RUN: %outopt -outline-only=1 -outlining-extractor -verify -S %s \
; RUN: | FileCheck --check-prefix=EXTRACT %s

; CHECK-LABEL: define void @f() {
define void @f() {
; CHECK: ; block 0
; CHECK-NEXT: ; node 1 dominating [0]
; CHECK-NEXT: %x = add i32 1, 2
  %x = add i32 1, 2
; CHECK-NEXT: ; node 2 prevents outlining
; CHECK-NEXT: ret void
  ret void

; CHECK-NOT: ; block 3{{.*}}
; CHECK-NOT: ; node 3{{.*}}
dead0:
  ret void

dead1:
  br i1 undef, label %dead0, label %dead1
}

; EXTRACT-LABEL: define void @f() {
; EXTRACT-NEXT: %1 = call fastcc {} @f.outlined.1.callee()
; EXTRACT-NEXT: ret void

; EXTRACT-LABEL: define fastcc {} @f.outlined.1.callee() unnamed_addr #0 {
; EXTRACT: outline_entry:
; EXTRACT: br label %0
; EXTRACT: outline_return:
; EXTRACT: ret {} undef
; EXTRACT: 0:
; EXTRACT: %x = add i32 1, 2
; EXTRACT: br label %outline_return

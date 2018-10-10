; RUN: llvm-as < %s | bc-split -o %t
; RUN: llvm-dis < %t/functions/f | FileCheck --check-prefix=DEFINE %s
; RUN: llvm-dis < %t/remainder/module | FileCheck --check-prefix=MODULE %s

; DEFINE: define void @0() prefix i8 12 prologue i8 34 {
; MODULE: define void @f() {
define void @f() prefix i8 12 prologue i8 34 {
  call void @g()
  ret void
}

; DEFINE-NOT: 56
; DEFINE-NOT: 78
; MODULE: declare void @g() prefix i8 56 prologue i8 78
declare void @g() prefix i8 56 prologue i8 78

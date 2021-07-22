; RUN: llvm-as < %s | bc-split -o %t
; RUN: llvm-dis < %t/functions/f.bc      | FileCheck --check-prefix=DEFINE %s
; RUN: llvm-dis < %t/remainder/module.bc | FileCheck --check-prefix=MODULE %s
; RUN: bc-join %t | llvm-dis             | FileCheck --check-prefix=JOINED %s

; DEFINE: define void @0() prefix i8 12 prologue i8 34 {
; MODULE: define void @f() {
; JOINED: define void @f() prefix i8 12 prologue i8 34 {
define void @f() prefix i8 12 prologue i8 34 {
  call void @g()
  ret void
}

; DEFINE-NOT: 56
; DEFINE-NOT: 78
; MODULE: declare void @g() prefix i8 56 prologue i8 78
; JOINED: declare void @g() prefix i8 56 prologue i8 78
declare void @g() prefix i8 56 prologue i8 78

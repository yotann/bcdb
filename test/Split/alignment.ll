; RUN: llvm-as < %s | bc-split - %t
; RUN: llvm-dis < %t/functions/f      | FileCheck --check-prefix=DEFINE %s
; RUN: llvm-dis < %t/remainder/module | FileCheck --check-prefix=MODULE %s

; MODULE: define void @f() align 16 {
; DEFINE: define void @0() align 16 {
define void @f() align 16 {
  call void @g()
  ret void
}

; MODULE: declare void @g() align 16
; DEFINE: declare void @g() align 16
declare void @g() align 16

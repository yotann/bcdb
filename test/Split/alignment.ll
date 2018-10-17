; RUN: llvm-as < %s | bc-split -o %t
; RUN: llvm-dis < %t/functions/f      | FileCheck --check-prefix=DEFINE %s
; RUN: llvm-dis < %t/remainder/module | FileCheck --check-prefix=MODULE %s
; RUN: bc-join %t | llvm-dis          | FileCheck --check-prefix=JOINED %s

; MODULE: define void @f() align 16 {
; DEFINE: define void @0() align 16 {
; JOINED: define void @f() align 16 {
define void @f() align 16 {
  call void @g()
  ret void
}

; MODULE: declare void @g() align 16
; DEFINE: declare void @g() align 16
; JOINED: declare void @g() align 16
declare void @g() align 16

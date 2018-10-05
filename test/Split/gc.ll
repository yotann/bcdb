; RUN: llvm-as < %s | bc-split - %t
; RUN: llvm-dis < %t/functions/f      | FileCheck --check-prefix=DEFINE %s
; RUN: llvm-dis < %t/remainder/module | FileCheck --check-prefix=MODULE %s

; MODULE: define void @f() gc "shadow-stack"
; DEFINE: define void @0() gc "shadow-stack"
define void @f() gc "shadow-stack" {
  call void @g()
  ret void
}

; MODULE: declare void @g() gc "shadow-stack"
; DEFINE: declare void @g() gc "shadow-stack"
declare void @g() gc "shadow-stack"

; RUN: llvm-as < %s | bc-split -o %t
; RUN: llvm-dis < %t/functions/f.bc      | FileCheck --check-prefix=DEFINE %s
; RUN: llvm-dis < %t/remainder/module.bc | FileCheck --check-prefix=MODULE %s
; RUN: bc-join %t | llvm-dis             | FileCheck --check-prefix=JOINED %s

; MODULE: define void @f() gc "shadow-stack"
; DEFINE: define void @0() gc "shadow-stack"
; JOINED: define void @f() gc "shadow-stack"
define void @f() gc "shadow-stack" {
  call void @g()
  ret void
}

; MODULE: declare void @g() gc "shadow-stack"
; DEFINE: declare void @g() gc "shadow-stack"
; JOINED: declare void @g() gc "shadow-stack"
declare void @g() gc "shadow-stack"

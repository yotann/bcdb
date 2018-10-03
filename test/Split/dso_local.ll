; REQUIRES: llvm7
; RUN: llvm-as < %s | bc-split - %t
; RUN: llvm-dis < %t/functions/f | FileCheck --check-prefix=F %s
; RUN: llvm-dis < %t/remainder/module | FileCheck --check-prefix=MODULE %s

; MODULE: declare dso_local void @f()
; F: define void @0()
define dso_local void @f() {
  call void @g()
  ret void
}

; MODULE: declare dso_local void @g()
; F: declare void @g()
declare dso_local void @g()

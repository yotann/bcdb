; RUN: llvm-as < %s | bc-split - %t
; RUN: llvm-dis < %t/functions/f.unnamed_addr | FileCheck --check-prefix=UNNAMED %s
; RUN: llvm-dis < %t/remainder/module | FileCheck --check-prefix=MODULE %s

; MODULE: declare void @f.unnamed_addr() unnamed_addr
; UNNAMED: define void @0()
define void @f.unnamed_addr() unnamed_addr {
  call void @f.local_unnamed_addr()
  ret void
}

; UNNAMED: declare void @f.local_unnamed_addr()
declare void @f.local_unnamed_addr() local_unnamed_addr

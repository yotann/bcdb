; RUN: llvm-as < %s | bc-split -o %t
; RUN: llvm-dis < %t/functions/f.unnamed_addr | FileCheck --check-prefix=DEFINE %s
; RUN: llvm-dis < %t/remainder/module | FileCheck --check-prefix=MODULE %s

; MODULE: define void @f.unnamed_addr() unnamed_addr
; MODULE-NEXT: unreachable
; DEFINE: define void @0()
define void @f.unnamed_addr() unnamed_addr {
  call void @f.local_unnamed_addr()
  ret void
}

; DEFINE: declare void @f.local_unnamed_addr()
declare void @f.local_unnamed_addr() local_unnamed_addr

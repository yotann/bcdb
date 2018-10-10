; RUN: llvm-as < %s | bc-split -o %t
; RUN: llvm-dis < %t/functions/f      | FileCheck --check-prefix=DEFINE %s
; RUN: llvm-dis < %t/remainder/module | FileCheck --check-prefix=MODULE %s
; RUN: bc-join %t | llvm-dis          | FileCheck --check-prefix=JOINED %s

; MODULE: define void @f() unnamed_addr
; MODULE-NEXT: unreachable
; DEFINE: define void @0()
; JOINED: define void @f() unnamed_addr
define void @f() unnamed_addr {
  call void @g()
  ret void
}

; DEFINE: declare void @g()
; JOINED: define void @g() local_unnamed_addr
define void @g() local_unnamed_addr {
  ret void
}

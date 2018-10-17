; REQUIRES: llvm7
; RUN: llvm-as < %s | bc-split -o %t
; RUN: llvm-dis < %t/functions/f      | FileCheck --check-prefix=DEFINE %s
; RUN: llvm-dis < %t/remainder/module | FileCheck --check-prefix=MODULE %s
; RUN: bc-join %t | llvm-dis          | FileCheck --check-prefix=JOINED %s

; MODULE: define dso_local void @f()
; MODULE-NEXT: unreachable
; DEFINE: define void @0()
; JOINED: define dso_local void @f()
define dso_local void @f() {
  call void @g()
  ret void
}

; MODULE: declare dso_local void @g()
; DEFINE: declare void @g()
; JOINED: declare dso_local void @g()
declare dso_local void @g()

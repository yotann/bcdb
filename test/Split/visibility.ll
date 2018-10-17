; RUN: llvm-as < %s | bc-split -o %t
; RUN: llvm-dis < %t/functions/f.hidden | FileCheck --check-prefix=DEFINE %s
; RUN: llvm-dis < %t/remainder/module | FileCheck --check-prefix=MODULE %s
; RUN: bc-join %t | llvm-dis          | FileCheck --check-prefix=JOINED %s

; MODULE: define hidden void @f.hidden()
; MODULE-NEXT: unreachable
; DEFINE: define void @0()
; JOINED: define hidden void @f.hidden()
define hidden void @f.hidden() {
  call void @f.protected()
  ret void
}

; DEFINE: declare void @f.protected()
; JOINED: declare protected void @f.protected()
declare protected void @f.protected()

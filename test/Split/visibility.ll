; RUN: llvm-as < %s | bc-split - %t
; RUN: llvm-dis < %t/functions/f.hidden | FileCheck --check-prefix=HIDDEN %s
; RUN: llvm-dis < %t/remainder/module | FileCheck --check-prefix=MODULE %s

; MODULE: define hidden void @f.hidden()
; MODULE-NEXT: unreachable
; HIDDEN: define void @0()
define hidden void @f.hidden() {
  call void @f.protected()
  ret void
}

; HIDDEN: declare void @f.protected()
declare protected void @f.protected()

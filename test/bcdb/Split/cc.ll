; RUN: llvm-as < %s | bc-split -o %t
; RUN: llvm-dis < %t/functions/f.fastcc.bc | FileCheck --check-prefix=DEFINE %s
; RUN: llvm-dis < %t/remainder/module.bc   | FileCheck --check-prefix=MODULE %s
; RUN: bc-join %t | llvm-dis               | FileCheck --check-prefix=JOINED %s

; MODULE: define fastcc void @f.fastcc()
; MODULE-NEXT: unreachable
; DEFINE: define fastcc void @0()
; JOINED: define fastcc void @f.fastcc()
define fastcc void @f.fastcc() {
  call void @f.coldcc()
  ret void
}

; MODULE: declare coldcc void @f.coldcc()
; DEFINE: declare coldcc void @f.coldcc()
; JOINED: declare coldcc void @f.coldcc()
declare coldcc void @f.coldcc()

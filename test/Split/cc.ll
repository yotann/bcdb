; RUN: llvm-as < %s | bc-split - %t
; RUN: llvm-dis < %t/functions/f.fastcc | FileCheck --check-prefix=FUNC %s
; RUN: llvm-dis < %t/remainder/module | FileCheck --check-prefix=MODULE %s

; MODULE: declare fastcc void @f.fastcc()
; FUNC: define fastcc void @0()
define fastcc void @f.fastcc() {
  call void @f.coldcc()
  ret void
}

; FUNC: declare coldcc void @f.coldcc()
declare coldcc void @f.coldcc()

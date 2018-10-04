; RUN: llvm-as < %s | bc-split - %t
; RUN: llvm-dis < %t/functions/f | FileCheck --check-prefix=DEFINE %s
; RUN: llvm-dis < %t/remainder/module | FileCheck --check-prefix=MODULE %s

; MODULE: define i32 @f(i32)
; MODULE-NEXT: unreachable

; DEFINE: define i32 @0(i32)
define i32 @f(i32) {
; DEFINE-NEXT: add i32 %0, %0
  add i32 %0, %0
; DEFINE-NEXT: ret i32 %2
  ret i32 %2
}

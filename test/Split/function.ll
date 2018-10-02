; RUN: llvm-as < %s | bc-split - %t
; RUN: llvm-dis < %t/functions/f | FileCheck --check-prefix=F %s
; RUN: llvm-dis < %t/remainder/module | FileCheck --check-prefix=MODULE %s

; MODULE: declare i32 @f(i32)
; F: define i32 @0(i32)
define i32 @f(i32) {
; F-NEXT: add i32 %0, %0
  add i32 %0, %0
; F-NEXT: ret i32 %2
  ret i32 %2
}

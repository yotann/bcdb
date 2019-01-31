; RUN: llvm-as < %s | bc-split -o %t
; RUN: llvm-dis < %t/functions/f.bc      | FileCheck --check-prefix=DEFINE %s
; RUN: llvm-dis < %t/remainder/module.bc | FileCheck --check-prefix=MODULE %s

; NOTE: bc-join doesn't work because of a bug in LLVM.
; https://bugs.llvm.org/show_bug.cgi?id=40368

define void @f() {
  call void @g()
  call void @h()
  ret void
}

; DEFINE: declare void @g()
; MODULE: @g = ifunc void (), i8* ()* @r
; JOINED: @g = ifunc void (), i8* ()* @r
@g = ifunc void (), i8* ()* @r

; DEFINE: declare void @h()
; MODULE: @h = weak hidden ifunc void (), i8* ()* @r
; JOINED: @h = weak hidden ifunc void (), i8* ()* @r
@h = weak hidden ifunc void (), i8* ()* @r

; DEFINE-NOT: @r
declare i8* @r()

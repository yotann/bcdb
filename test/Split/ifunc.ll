; RUN: llvm-as < %s | bc-split -o %t
; RUN: llvm-dis < %t/functions/f      | FileCheck --check-prefix=DEFINE %s
; RUN: llvm-dis < %t/remainder/module | FileCheck --check-prefix=MODULE %s
; RUN: bc-join %t | llvm-dis          | FileCheck --check-prefix=JOINED %s
; XFAIL: *

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

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
; MODULE: @g = ifunc void (), void ()* ()* @r
; JOINED: @g = ifunc void (), void ()* ()* @r
@g = ifunc void (), void ()* ()* @r

; DEFINE: declare void @h()
; MODULE: @h = weak hidden ifunc void (), void ()* ()* @r
; JOINED: @h = weak hidden ifunc void (), void ()* ()* @r
@h = weak hidden ifunc void (), void ()* ()* @r

; DEFINE-NOT: @r
declare void ()* @r()

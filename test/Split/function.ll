; RUN: llvm-as < %s | bc-split - %t
; RUN: llvm-dis < %t/functions/f | FileCheck --check-prefix=DEFINE %s
; RUN: llvm-dis < %t/remainder/module | FileCheck --check-prefix=MODULE %s

; MODULE: define i32 @f(i32) {
; MODULE-NEXT: unreachable

define i32 @f(i32 %arg) !srcloc !0 {
; DEFINE: define i32 @0(i32 %arg) !srcloc !0 {
entry:
; DEFINE-NEXT: entry:
  add i32 %arg, %arg
  ; DEFINE-NEXT: add i32 %arg, %arg
  %swap = call i32 asm "bswap $0", "=r,r"(i32 %arg)
  ; DEFINE-NEXT: %swap = call i32 asm "bswap $0", "=r,r"(i32 %arg)
  call i32 @llvm.read_register.i32(metadata !1), !srcloc !2
  ; DEFINE-NEXT: call i32 @llvm.read_register.i32(metadata !1)
  call void @g()
  ; DEFINE-NEXT: call void @g()
  ret i32 %0
  ; DEFINE-NEXT: ret i32 %0
}

declare i32 @llvm.read_register.i32(metadata)
; DEFINE: declare i32 @llvm.read_register.i32(metadata)

declare !srcloc !3 void @g()
; DEFINE: declare void @g()
; MODULE: declare !srcloc !0 void @g()

!0 = !{!"blah"}
; DEFINE: !0 = !{!"blah"}

!1 = !{!"rax"}
; DEFINE: !1 = !{!"rax"}
; MODULE-NOT: !1

!2 = distinct !{!1}
; DEFINE: !2 = distinct !{!1}
; MODULE-NOT: !2

!3 = !{i32 1234}
; DEFINE-NOT: !3
; MODULE: !0 = !{i32 1234}

; RUN: llvm-as < %s | bc-split - %t
; RUN: llvm-dis < %t/functions/f | FileCheck --check-prefix=DEFINE %s

; DEFINE: %s = type { i32 }
; DEFINE: %t = type { i32, i32 }
%s = type { i32 }
%t = type { i32, i32 }

define void @f(%s) {
  call %t asm "quux", "=r,=r,r"(%s %0)
  ret void
}

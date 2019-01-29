; RUN: llvm-as < %s | bc-split -o %t
; RUN: llvm-dis < %t/functions/f | FileCheck --check-prefix=DEFINE %s
; XFAIL: *

; DEFINE: %0 = type { i32 }
; DEFINE: %1 = type { i32, i32 }
%s = type { i32 }
%t = type { i32, i32 }

define void @f(%s) {
  call %t asm "quux", "=r,=r,r"(%s %0)
  ret void
}

; RUN: llvm-as < %s | bc-split -o %t
; RUN: llvm-dis < %t/functions/f | FileCheck --check-prefix=DEFINE %s

; DEFINE: %s0 = type { i16 }
%s0 = type { i16 }

@g0 = global %s0 zeroinitializer

define void @f() {
  load i16, i16* getelementptr (%s0, %s0* @g0, i64 0, i32 0)
  ret void
}

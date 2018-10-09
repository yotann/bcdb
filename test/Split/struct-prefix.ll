; RUN: llvm-as < %s | bc-split - %t
; RUN: llvm-dis < %t/functions/f | FileCheck --check-prefix=DEFINE %s

; DEFINE: %s0 = type { i16 }
; DEFINE: %s1 = type { i32 }
%s0 = type { i16 }
%s1 = type { i32 }

define void @f() prefix %s0 zeroinitializer prologue %s1 zeroinitializer {
  ret void
}
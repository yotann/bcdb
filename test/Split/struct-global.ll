; RUN: llvm-as < %s | bc-split -o %t
; RUN: llvm-dis < %t/functions/f | FileCheck --check-prefix=DEFINE %s

; DEFINE: %s0 = type opaque
%s0 = type { i16 }

@g0 = global %s0 zeroinitializer

define void @f(%s0*) {
  call void @f(%s0* @g0)
  ret void
}

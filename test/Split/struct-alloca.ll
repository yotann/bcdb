; RUN: llvm-as < %s | bc-split -o %t
; RUN: llvm-dis < %t/functions/f.bc | FileCheck --check-prefix=DEFINE %s

; DEFINE: %0 = type { i16 }
%s = type { i16 }

define void @f() {
  alloca %s
  ret void
}

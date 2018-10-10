; RUN: llvm-as < %s | bc-split -o %t
; RUN: llvm-dis < %t/functions/f | FileCheck --check-prefix=DEFINE %s

; DEFINE: %s = type { i16 }
%s = type { i16 }

define void @f() {
  alloca %s
  ret void
}

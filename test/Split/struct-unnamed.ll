; RUN: llvm-as < %s | bc-split -o %t
; RUN: llvm-dis < %t/functions/f.bc      | FileCheck --check-prefix=DEFINE %s
; RUN: llvm-dis < %t/remainder/module.bc | FileCheck --check-prefix=MODULE %s

; DEFINE: %0 = type { %0* }
; MODULE: %0 = type { %0* }
%0 = type { %0* }

define void @f(%0) {
  ret void
}

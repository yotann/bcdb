; RUN: llvm-as < %s | bc-split -o %t
; RUN: llvm-dis < %t/functions/f.bc      | FileCheck --check-prefix=DEFINE %s
; RUN: llvm-dis < %t/remainder/module.bc | FileCheck --check-prefix=MODULE %s

; DEFINE-NOT: type { i32 }
; MODULE: %struct = type { i32 }
%struct = type { i32 }

define void @f(<{ <4 x %struct*> }>) {
  ret void
}

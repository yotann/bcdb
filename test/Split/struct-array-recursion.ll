; RUN: llvm-as < %s | bc-split -o %t
; RUN: llvm-dis < %t/functions/f | FileCheck --check-prefix=DEFINE %s
; RUN: llvm-dis < %t/remainder/module | FileCheck --check-prefix=MODULE %s

; DEFINE: %struct = type { [1 x %struct]* }
; MODULE: %struct = type { [1 x %struct]* }
%struct = type { [1 x %struct]* }

define void @f([1 x %struct]) {
  ret void
}

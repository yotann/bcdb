; RUN: llvm-as < %s | bc-split -o %t
; RUN: llvm-dis < %t/functions/f.bc      | FileCheck --check-prefix=DEFINE %s
; RUN: llvm-dis < %t/remainder/module.bc | FileCheck --check-prefix=MODULE %s

; DEFINE: %struct = type opaque
; MODULE: %struct = type opaque
%struct = type opaque

define void @f(%struct) {
  ret void
}

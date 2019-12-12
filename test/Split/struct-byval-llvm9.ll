; REQUIRES: llvm9
; RUN: llvm-as < %s | bc-split -o %t
; RUN: llvm-dis < %t/functions/f.bc      | FileCheck %s

; CHECK: %0 = type opaque
; CHECK: %s = type { %0* }
%struct2 = type { i32 }
%struct = type { %struct2* }

; CHECK: define void @0(%s* byval(%s) %arg) {
define void @f(%struct* byval(%struct) %arg) {
  ret void
}

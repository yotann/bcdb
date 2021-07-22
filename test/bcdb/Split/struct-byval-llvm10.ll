; REQUIRES: llvm10
; RUN: llvm-as < %s | bc-split -o %t
; RUN: llvm-dis < %t/functions/f.bc      | FileCheck %s
; RUN: bc-join %t   | opt -verify -S     | FileCheck --check-prefix=JOINED %s

; CHECK: %0 = type { %1* }
; CHECK: %1 = type opaque
%struct2 = type { i32 }
%struct = type { %struct2* }

; CHECK: define void @0(%0* byval(%0) %arg) {
; JOINED: define void @f(%struct* byval(%struct) %arg) {
define void @f(%struct* byval(%struct) %arg) {
  ret void
}

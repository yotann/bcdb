; RUN: llvm-as < %s | bc-split -o %t
; RUN: llvm-dis < %t/functions/f      | FileCheck --check-prefix=DEFINE %s
; RUN: llvm-dis < %t/remainder/module | FileCheck --check-prefix=MODULE %s

; MODULE: define void @f() section "pics" {
; DEFINE: define void @0() {
define void @f() section "pics" {
  ret void
}

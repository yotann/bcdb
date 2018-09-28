; RUN: llvm-cat -o - %s %S/Inputs/multi-module-f2.ll | bc-align > %t
; RUN: llvm-modextract -n 0 -o - %t | llvm-dis | FileCheck --check-prefix=IR1 %s
; RUN: llvm-modextract -n 1 -o - %t | llvm-dis | FileCheck --check-prefix=IR2 %s

; RUN: llvm-as -o %t1 %s
; RUN: llvm-as -o %t2 %S/Inputs/multi-module-f2.ll
; RUN: llvm-cat -b -o %t %t1 %t2
; RUN: llvm-cat -b -o - %t %t | bc-align > %t3
; RUN: llvm-modextract -n 0 -o - %t3 | llvm-dis | FileCheck --check-prefix=IR1 %s
; RUN: llvm-modextract -n 1 -o - %t3 | llvm-dis | FileCheck --check-prefix=IR2 %s
; RUN: llvm-modextract -n 2 -o - %t3 | llvm-dis | FileCheck --check-prefix=IR1 %s
; RUN: llvm-modextract -n 3 -o - %t3 | llvm-dis | FileCheck --check-prefix=IR2 %s

; IR1: define void @f1()
; IR2: define void @f2()

define void @f1() {
  ret void
}

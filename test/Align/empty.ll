; RUN: llvm-as < %s | llvm-dis > %t1
; RUN: llvm-as < %s | bc-align | llvm-dis > %t2
; RUN: diff %t1 %t2

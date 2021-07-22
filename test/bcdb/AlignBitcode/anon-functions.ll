; RUN: llvm-as < %s            | llvm-dis > %t1
; RUN: llvm-as < %s | bc-align | llvm-dis > %t2
; RUN: diff %t1 %t2

define void @0() {
  ret void
}

define void @f() {
  ret void
}

define void @1() {
  ret void
}

; RUN: bcdb init -uri sqlite:%t
; RUN: llvm-as < %s | bcdb add -uri sqlite:%t -
; RUN: bcdb merge -uri sqlite:%t - | opt -verify -S

%struct = type opaque

define void @empty(%struct*) {
  ret void
}

define void @func(%struct*) {
  call void @empty(%struct* %0)
  call void @func(%struct* %0)
  ret void
}

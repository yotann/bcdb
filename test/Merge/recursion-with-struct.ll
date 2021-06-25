; RUN: bcdb init -store sqlite:%t
; RUN: llvm-as < %s | bcdb add -store sqlite:%t -
; RUN: bcdb merge -store sqlite:%t - | opt -verify -S

%struct = type opaque

define void @empty(%struct*) {
  ret void
}

define void @func(%struct*) {
  call void @empty(%struct* %0)
  call void @func(%struct* %0)
  ret void
}

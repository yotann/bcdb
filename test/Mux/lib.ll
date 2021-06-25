; RUN: bcdb init -store sqlite:%t
; RUN: llvm-as < %s | bcdb add -store sqlite:%t - -name bin
; RUN: llvm-as < %p/Inputs/lib.ll | bcdb add -store sqlite:%t - -name lib
; RUN: bcdb mux -store sqlite:%t bin lib | lli - bin

; RUN: bcdb init -store sqlite:%t.rg
; RUN: llvm-as < %s | bcdb add -store sqlite:%t.rg - -name bin -rename-globals
; RUN: llvm-as < %p/Inputs/lib.ll | bcdb add -store sqlite:%t.rg - -name lib -rename-globals
; RUN: bcdb mux -store sqlite:%t.rg bin lib | lli - bin

@global = external global i32

declare void @set_global_to_0()

define i32 @main(i32, i8**) {
  call void @set_global_to_0()
  %x = load i32, i32* @global
  ret i32 %x
}

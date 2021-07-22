; RUN: memodb init -store sqlite:%t
; RUN: llvm-as < %s | bcdb add -store sqlite:%t - -name bin
; RUN: llvm-as < %p/Inputs/global-common.ll | bcdb add -store sqlite:%t - -name lib
; RUN: bcdb mux -store sqlite:%t bin lib | lli - bin

; RUN: memodb init -store sqlite:%t.rg
; RUN: llvm-as < %s | bcdb add -rename-globals -store sqlite:%t.rg - -name bin
; RUN: llvm-as < %p/Inputs/global-common.ll | bcdb add -rename-globals -store sqlite:%t.rg - -name lib
; RUN: bcdb mux -store sqlite:%t.rg bin lib | lli - bin

@global = external global i32

declare void @set_global_to_1()

define i32 @main(i32, i8**) {
  call void @set_global_to_1()
  %x = load i32, i32* @global
  %y = sub i32 1, %x
  ret i32 %y
}

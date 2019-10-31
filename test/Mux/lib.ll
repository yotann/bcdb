; RUN: bcdb init -uri sqlite:%t
; RUN: llvm-as < %s | bcdb add -uri sqlite:%t - -name bin
; RUN: llvm-as < %p/Inputs/lib.ll | bcdb add -uri sqlite:%t - -name lib
; RUN: bcdb mux -uri sqlite:%t bin lib | lli - bin

@global = external global i32

declare void @set_global_to_0()

define i32 @main(i32, i8**) {
  call void @set_global_to_0()
  %x = load i32, i32* @global
  ret i32 %x
}

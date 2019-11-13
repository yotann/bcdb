; RUN: bcdb init -uri sqlite:%t
; RUN: llvm-as < %s | bcdb add -uri sqlite:%t - -name bin
; RUN: llvm-as < %p/Inputs/lib.ll | bcdb add -uri sqlite:%t - -name lib
; RUN: llvm-as < %p/Inputs/weak.ll | bcdb add -uri sqlite:%t - -name weak
; RUN: llvm-as < %p/Inputs/weak.ll | bcdb add -uri sqlite:%t - -name weak2
; RUN: bcdb mux -uri sqlite:%t bin lib weak weak2 | lli - bin

@global = external global i32

declare void @set_global_to_0()
declare void @only_defined_weakly()

define i32 @main(i32, i8**) {
  call void @only_defined_weakly()
  call void @set_global_to_0()
  %x = load i32, i32* @global
  ret i32 %x
}

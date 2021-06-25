; RUN: bcdb init -store sqlite:%t
; RUN: llvm-as < %s | bcdb add -store sqlite:%t - -name x
; RUN: bcdb mux -store sqlite:%t x | lli - x

; RUN: bcdb init -store sqlite:%t.rg
; RUN: llvm-as < %s | bcdb add -store sqlite:%t.rg - -name x -rename-globals
; RUN: bcdb mux -store sqlite:%t.rg x | lli - x

define i32 @main(i32, i8**) {
  ret i32 0
}

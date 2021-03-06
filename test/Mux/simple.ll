; RUN: bcdb init -uri sqlite:%t
; RUN: llvm-as < %s | bcdb add -uri sqlite:%t - -name x
; RUN: bcdb mux -uri sqlite:%t x | lli - x

; RUN: bcdb init -uri sqlite:%t.rg
; RUN: llvm-as < %s | bcdb add -uri sqlite:%t.rg - -name x -rename-globals
; RUN: bcdb mux -uri sqlite:%t.rg x | lli - x

define i32 @main(i32, i8**) {
  ret i32 0
}

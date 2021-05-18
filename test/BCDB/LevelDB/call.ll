; RUN: rm -r %t
; RUN: bcdb init -uri leveldb:%t
; RUN: llvm-as < %s | bcdb add -uri leveldb:%t -name a -

; RUN: not bcdb evaluate -uri leveldb:%t myfunc $(bcdb list-function-ids -uri leveldb:%t)

; RUN: bcdb cache -uri leveldb:%t -result $(bcdb list-function-ids -uri leveldb:%t) myfunc $(bcdb list-function-ids -uri leveldb:%t)
; RUN: bcdb evaluate -uri leveldb:%t myfunc $(bcdb list-function-ids -uri leveldb:%t)

; RUN: bcdb invalidate -uri leveldb:%t myfunc
; RUN: not bcdb evaluate -uri leveldb:%t myfunc $(bcdb list-function-ids -uri leveldb:%t)

define i32 @func(i32 %x, i32 %y) {
  %z = add i32 %x, %y
  ret i32 %z
}

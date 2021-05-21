; RUN: rm -rf %t
; RUN: bcdb init -uri leveldb:%t
; RUN: llvm-as < %s | bcdb add -uri leveldb:%t -name a -

; RUN: not bcdb evaluate -uri leveldb:%t myfunc $(bcdb list-function-ids -uri leveldb:%t)

; RUN: bcdb cache -uri leveldb:%t -result $(bcdb list-function-ids -uri leveldb:%t) myfunc $(bcdb list-function-ids -uri leveldb:%t)
; RUN: bcdb evaluate -uri leveldb:%t myfunc $(bcdb list-function-ids -uri leveldb:%t)

; RUN: memodb refs-to -uri leveldb:%t id:$(bcdb list-function-ids -uri leveldb:%t) | FileCheck --check-prefix=REFS %s
; REFS: call:myfunc/{{[-A-Za-z0-9_=]+$}}

; RUN: memodb list-calls -uri leveldb:%t myfunc | FileCheck --check-prefix=CALLS %s
; CALLS: call:myfunc/{{[-A-Za-z0-9_=]+$}}

; RUN: bcdb invalidate -uri leveldb:%t myfunc
; RUN: not bcdb evaluate -uri leveldb:%t myfunc $(bcdb list-function-ids -uri leveldb:%t)

define i32 @func(i32 %x, i32 %y) {
  %z = add i32 %x, %y
  ret i32 %z
}

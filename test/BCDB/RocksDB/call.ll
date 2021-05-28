; RUN: rm -rf %t
; RUN: bcdb init -uri rocksdb:%t
; RUN: llvm-as < %s | bcdb add -uri rocksdb:%t -name a -

; RUN: not bcdb evaluate -uri rocksdb:%t myfunc $(bcdb list-function-ids -uri rocksdb:%t)

; RUN: bcdb cache -uri rocksdb:%t -result $(bcdb list-function-ids -uri rocksdb:%t) myfunc $(bcdb list-function-ids -uri rocksdb:%t)
; RUN: bcdb evaluate -uri rocksdb:%t myfunc $(bcdb list-function-ids -uri rocksdb:%t)

; RUN: memodb refs-to -uri rocksdb:%t id:$(bcdb list-function-ids -uri rocksdb:%t) | FileCheck --check-prefix=REFS %s
; REFS: call:myfunc/{{[-A-Za-z0-9_=]+$}}

; RUN: memodb list-calls -uri rocksdb:%t myfunc | FileCheck --check-prefix=CALLS %s
; CALLS: call:myfunc/{{[-A-Za-z0-9_=]+$}}

; RUN: bcdb invalidate -uri rocksdb:%t myfunc
; RUN: not bcdb evaluate -uri rocksdb:%t myfunc $(bcdb list-function-ids -uri rocksdb:%t)

define i32 @func(i32 %x, i32 %y) {
  %z = add i32 %x, %y
  ret i32 %z
}

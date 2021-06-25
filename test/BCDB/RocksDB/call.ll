; RUN: rm -rf %t
; RUN: bcdb init -store rocksdb:%t
; RUN: llvm-as < %s | bcdb add -store rocksdb:%t -name a -

; RUN: not bcdb evaluate -store rocksdb:%t myfunc $(bcdb list-function-ids -store rocksdb:%t)

; RUN: bcdb cache -store rocksdb:%t -result $(bcdb list-function-ids -store rocksdb:%t) myfunc $(bcdb list-function-ids -store rocksdb:%t)
; RUN: bcdb evaluate -store rocksdb:%t myfunc $(bcdb list-function-ids -store rocksdb:%t)

; RUN: memodb refs-to -store rocksdb:%t id:$(bcdb list-function-ids -store rocksdb:%t) | FileCheck --check-prefix=REFS %s
; REFS: call:myfunc/{{[-A-Za-z0-9_=]+$}}

; RUN: memodb list-calls -store rocksdb:%t myfunc | FileCheck --check-prefix=CALLS %s
; CALLS: call:myfunc/{{[-A-Za-z0-9_=]+$}}

; RUN: bcdb invalidate -store rocksdb:%t myfunc
; RUN: not bcdb evaluate -store rocksdb:%t myfunc $(bcdb list-function-ids -store rocksdb:%t)

define i32 @func(i32 %x, i32 %y) {
  %z = add i32 %x, %y
  ret i32 %z
}

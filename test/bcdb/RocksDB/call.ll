; RUN: rm -rf %t
; RUN: memodb init -store rocksdb:%t
; RUN: llvm-as < %s | bcdb add -store rocksdb:%t -name a -

; RUN: not memodb put -store rocksdb:%t /call/myfunc/$(bcdb list-function-ids -store rocksdb:%t)

; RUN: memodb set -store rocksdb:%t /call/myfunc/$(bcdb list-function-ids -store rocksdb:%t) /cid/$(bcdb list-function-ids -store rocksdb:%t)
; RUN: memodb put -store rocksdb:%t /call/myfunc/$(bcdb list-function-ids -store rocksdb:%t)

; RUN: memodb refs-to -store rocksdb:%t /cid/$(bcdb list-function-ids -store rocksdb:%t) | FileCheck --check-prefix=REFS %s
; REFS: /call/myfunc/{{[-A-Za-z0-9_=]+$}}

; RUN: memodb list-calls -store rocksdb:%t myfunc | FileCheck --check-prefix=CALLS %s
; CALLS: /call/myfunc/{{[-A-Za-z0-9_=]+$}}

; RUN: memodb invalidate -store rocksdb:%t myfunc
; RUN: not memodb put -store rocksdb:%t /call/myfunc/$(bcdb list-function-ids -store rocksdb:%t)

define i32 @func(i32 %x, i32 %y) {
  %z = add i32 %x, %y
  ret i32 %z
}
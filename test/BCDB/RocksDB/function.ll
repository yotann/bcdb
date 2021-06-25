; RUN: rm -rf %t
; RUN: bcdb init -store rocksdb:%t
; RUN: llvm-as < %s | bcdb add -store rocksdb:%t -
; RUN: bcdb get -store rocksdb:%t -name - | opt -verify -S | FileCheck %s
; RUN: bcdb get-function -store rocksdb:%t -id $(bcdb list-function-ids -store rocksdb:%t) | opt -verify -S | FileCheck --check-prefix=FUNC %s

; FUNC: define i32 @0(i32 %x, i32 %y)
; CHECK: define i32 @func(i32 %x, i32 %y)
define i32 @func(i32 %x, i32 %y) {
  ; FUNC: %z = add i32 %x, %y
  ; CHECK: %z = add i32 %x, %y
  %z = add i32 %x, %y
  ; FUNC: ret i32 %z
  ; CHECK: ret i32 %z
  ret i32 %z
}

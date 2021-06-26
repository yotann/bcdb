; RUN: rm -rf %t
; RUN: memodb init -store rocksdb:%t
; RUN: bcdb add -store rocksdb:%t %s -name x
; RUN: memodb set -store rocksdb:%t head:y head:x
; RUN: bcdb get -store rocksdb:%t -name y | opt -verify -S | FileCheck %s

; CHECK: define i32 @func(i32 %x, i32 %y)
define i32 @func(i32 %x, i32 %y) {
  ; CHECK: %z = add i32 %x, %y
  %z = add i32 %x, %y
  ; CHECK: ret i32 %z
  ret i32 %z
}

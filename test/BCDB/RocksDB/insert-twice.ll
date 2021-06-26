; RUN: rm -rf %t
; RUN: memodb init -store rocksdb:%t
; RUN: llvm-as < %s | bcdb add -store rocksdb:%t -name a -
; RUN: llvm-as < %s | bcdb add -store rocksdb:%t -name a -
; RUN: llvm-as < %s | bcdb add -store rocksdb:%t -name b -
; RUN: bcdb get -store rocksdb:%t -name a | opt -verify -S | FileCheck %s
; RUN: bcdb get -store rocksdb:%t -name b | opt -verify -S | FileCheck %s
; RUN: memodb paths-to -store rocksdb:%t id:$(bcdb list-function-ids -store rocksdb:%t) | FileCheck --check-prefix=REFS %s

; CHECK: define i32 @func(i32 %x, i32 %y)
define i32 @func(i32 %x, i32 %y) {
  ; CHECK: %z = add i32 %x, %y
  %z = add i32 %x, %y
  ; CHECK: ret i32 %z
  ret i32 %z
}

; REFS: heads["a"]["functions"]["func"]
; REFS: heads["b"]["functions"]["func"]

; RUN: rm -rf %t
; RUN: bcdb init -uri leveldb:%t
; RUN: llvm-as < %s | bcdb add -uri leveldb:%t -name a -
; RUN: llvm-as < %s | bcdb add -uri leveldb:%t -name a -
; RUN: llvm-as < %s | bcdb add -uri leveldb:%t -name b -
; RUN: bcdb get -uri leveldb:%t -name a | opt -verify -S | FileCheck %s
; RUN: bcdb get -uri leveldb:%t -name b | opt -verify -S | FileCheck %s
; RUN: bcdb refs -uri leveldb:%t $(bcdb list-function-ids -uri leveldb:%t) | FileCheck --check-prefix=REFS %s

; CHECK: define i32 @func(i32 %x, i32 %y)
define i32 @func(i32 %x, i32 %y) {
  ; CHECK: %z = add i32 %x, %y
  %z = add i32 %x, %y
  ; CHECK: ret i32 %z
  ret i32 %z
}

; REFS: heads["a"]["functions"]["func"]
; REFS: heads["b"]["functions"]["func"]

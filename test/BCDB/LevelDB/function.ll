; RUN: rm -r %t
; RUN: bcdb init -uri leveldb:%t
; RUN: llvm-as < %s | bcdb add -uri leveldb:%t -
; RUN: bcdb get -uri leveldb:%t -name - | opt -verify -S | FileCheck %s
; RUN: bcdb get-function -uri leveldb:%t -id $(bcdb list-function-ids -uri leveldb:%t) | opt -verify -S | FileCheck --check-prefix=FUNC %s

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

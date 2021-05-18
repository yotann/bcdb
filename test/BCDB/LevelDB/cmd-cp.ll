; RUN: rm -rf %t
; RUN: bcdb init -uri leveldb:%t
; RUN: bcdb add -uri leveldb:%t %s -name x
; RUN: bcdb cp -uri leveldb:%t x y
; RUN: bcdb get -uri leveldb:%t -name y | opt -verify -S | FileCheck %s

; CHECK: define i32 @func(i32 %x, i32 %y)
define i32 @func(i32 %x, i32 %y) {
  ; CHECK: %z = add i32 %x, %y
  %z = add i32 %x, %y
  ; CHECK: ret i32 %z
  ret i32 %z
}

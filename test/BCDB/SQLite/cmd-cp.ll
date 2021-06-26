; RUN: rm -rf %t
; RUN: memodb init -store sqlite:%t
; RUN: bcdb add -store sqlite:%t %s -name x
; RUN: memodb set -store sqlite:%t head:y head:x
; RUN: bcdb get -store sqlite:%t -name y | opt -verify -S | FileCheck %s

; CHECK: define i32 @func(i32 %x, i32 %y)
define i32 @func(i32 %x, i32 %y) {
  ; CHECK: %z = add i32 %x, %y
  %z = add i32 %x, %y
  ; CHECK: ret i32 %z
  ret i32 %z
}

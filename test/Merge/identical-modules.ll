; RUN: bcdb init -uri sqlite:%t
; RUN: llvm-as < %s | bcdb add -uri sqlite:%t - -name a
; RUN: llvm-as < %s | bcdb add -uri sqlite:%t - -name b
; RUN: bcdb merge -uri sqlite:%t a b | opt -verify -S | FileCheck %s

define i32 @func(i32 %x) {
  ret i32 %x
}
; CHECK: define i32 @func
; CHECK-NOT: define i32 @func

define i32 @caller(i32 %y) {
  %z = call i32 @func(i32 %y)
  ret i32 %z
}
; CHECK: define i32 @caller
; CHECK-NOT: define i32 @caller

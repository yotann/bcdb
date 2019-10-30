; RUN: bcdb init -uri sqlite:%t
; RUN: llvm-as < %s | bcdb add -uri sqlite:%t -
; RUN: bcdb merge -uri sqlite:%t - | opt -verify -S | FileCheck %s

define i32 @func(i32 %x) {
  %y = add i32 %x, 1351
  ret i32 %y
}

; CHECK: define internal i32 @__bcdb_id_[[ID0:.*]](i32 %x) {
; CHECK-NEXT: %y = add i32 %x, 1351
; CHECK-NEXT: ret i32 %y

; CHECK: define i32 @func(i32) {
; CHECK-NEXT: musttail call i32 @__bcdb_id_[[ID0]](i32 %0)

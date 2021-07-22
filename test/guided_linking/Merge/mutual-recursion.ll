; RUN: memodb init -store sqlite:%t
; RUN: llvm-as < %s | bcdb add -store sqlite:%t -
; RUN: bcdb merge -store sqlite:%t - | opt -verify -S | FileCheck %s

define i32 @f0() {
  call i32 @f1()
  ret i32 %1
}
; CHECK: define internal i32 @__bcdb_body_f0() {
; CHECK-NEXT: call i32 @f1()
; CHECK-NEXT: ret i32 %1

define i32 @f1() {
  call i32 @f0()
  ret i32 %1
}
; CHECK: define internal i32 @__bcdb_body_f1() {
; CHECK-NEXT: call i32 @f0()
; CHECK-NEXT: ret i32 %1

; CHECK: define i32 @f0() #0 {
; CHECK-NEXT: tail call i32 @__bcdb_body_f0()
; CHECK: define i32 @f1() #0 {
; CHECK-NEXT: tail call i32 @__bcdb_body_f1()

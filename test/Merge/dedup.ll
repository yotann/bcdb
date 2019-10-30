; RUN: bcdb init -uri sqlite:%t
; RUN: llvm-as < %s | bcdb add -uri sqlite:%t -
; RUN: bcdb merge -uri sqlite:%t - | opt -verify -S | FileCheck %s

define i32 @f0() {
  ret i32 1351
}
; CHECK: define internal i32 @__bcdb_id_[[ID0:.*]]() {
; CHECK-NEXT: ret i32 1351
; CHECK: define i32 @f0() {
; CHECK-NEXT: musttail call i32 @__bcdb_id_[[ID0]]()

define i32 @f1() {
  ret i32 1351
}
; CHECK-NOT: define {{.*}} @__bcdb_id_
; CHECK: define i32 @f1() {
; CHECK-NEXT: musttail call i32 @__bcdb_id_[[ID0]]()

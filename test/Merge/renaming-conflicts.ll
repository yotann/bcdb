; RUN: bcdb init -uri sqlite:%t
; RUN: llvm-as < %s | bcdb add -uri sqlite:%t - -name a
; RUN: llvm-as < %p/Inputs/renaming-conflicts.ll | bcdb add -uri sqlite:%t - -name b
; RUN: bcdb merge -uri sqlite:%t a b | opt -verify -S | FileCheck %s

; CHECK-DAG: @g0{{.*}} = internal constant
; CHECK-DAG: @g1{{.*}} = internal constant
; CHECK-DAG: @g0{{.*}} = internal constant
; CHECK-DAG: @g1{{.*}} = internal constant
@g0 = internal constant i8* bitcast (void()* @f0 to i8*)
@g1 = internal constant i8* bitcast (void()* @f1 to i8*)

define internal void @f0() {
  ret void
}

define internal void @f1() {
  ret void
}

define void @main() {
  load i8*, i8** @g0
  load i8*, i8** @g1
  call void @f0()
  call void @f1()
  ret void
}

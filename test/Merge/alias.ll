; RUN: bcdb init -uri sqlite:%t
; RUN: llvm-as < %s | bcdb add -uri sqlite:%t - -name a
; RUN: bcdb merge -uri sqlite:%t a | opt -verify -S | FileCheck %s

; CHECK-NOT: @X.
@X = internal global i32 12

; CHECK-NOT: @f.
define internal void @f() {
  ret void
}

; CHECK-DAG: @Y = internal alias i32, i32* @X
@Y = internal alias i32, i32* @X

; CHECK-DAG: @g = internal alias void (), void ()* @f
@g = internal alias void (), void ()* @f

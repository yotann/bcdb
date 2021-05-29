; RUN: rm -rf %t
; RUN: bcdb init -uri sqlite:%t
; RUN: llvm-as < %s | bcdb add -uri sqlite:%t -
; RUN: bcdb get -uri sqlite:%t -name - | opt -verify -S | FileCheck %s

; CHECK: @a = internal alias void (), void ()* @func
@a = internal alias void (), void ()* @func

; CHECK-LABEL: define void @func()
define void @func() {
  ; CHECK-NEXT: ret void
  ret void
}

; CHECK-LABEL: define void @main()
define void @main() {
  ; CHECK-NEXT: call void @a()
  call void @a()
  ret void
}

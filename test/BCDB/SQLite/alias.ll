; RUN: rm -rf %t
; RUN: memodb init -store sqlite:%t
; RUN: llvm-as < %s | bcdb add -store sqlite:%t -
; RUN: bcdb get -store sqlite:%t -name - | opt -verify -S | FileCheck %s

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

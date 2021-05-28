; RUN: rm -rf %t
; RUN: bcdb init -uri rocksdb:%t
; RUN: llvm-as < %s | bcdb add -uri rocksdb:%t -
; RUN: bcdb get -uri rocksdb:%t -name - | opt -verify -S | FileCheck %s

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

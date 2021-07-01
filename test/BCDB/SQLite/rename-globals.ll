; RUN: rm -rf %t
; RUN: memodb init -store sqlite:%t
; RUN: llvm-as < %s | bcdb add -store sqlite:%t - -rename-globals
; RUN: bcdb get -store sqlite:%t -name - | opt -verify -S | FileCheck %s

; CHECK: @__bcdb_alias_[[ID:[-_=0-9A-Za-z]+]] = internal alias void (), void ()* @func

; CHECK-LABEL: define void @func()
define void @func() {
  ; CHECK-NEXT: call void @func()
  call void @func()
  ret void
}

; CHECK-LABEL: define void @main()
define void @main() {
  ; CHECK-NEXT: call void @__bcdb_alias_[[ID]]()
  call void @func()
  ret void
}

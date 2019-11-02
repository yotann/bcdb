; XFAIL: *
; RUN: bcdb init -uri sqlite:%t
; RUN: llvm-as < %s | bcdb add -uri sqlite:%t -
; RUN: bcdb merge -uri sqlite:%t - | opt -verify -S | FileCheck %s

%struct.va_list = type { i8* }

declare void @llvm.va_start(i8*)
declare void @llvm.va_end(i8*)

define void @func(i32 %x, ...) {
  %ap = alloca %struct.va_list
  %ap2 = bitcast %struct.va_list* %ap to i8*
  call void @llvm.va_start(i8* %ap2)
  %y = va_arg i8* %ap2, i32
  call void @llvm.va_end(i8* %ap2)
  call void (i32, ...) @func(i32 %y, i32 %x)
  ret void
}
; CHECK: define internal void @__bcdb_id_[[ID0:.*]](i32 %x, ...) #0 {
; CHECK: call void (i32, ...) @func(i32 %y, i32 %x)
; CHECK: define void @func(i32, ...) #0 {
; CHECK-NEXT: musttail call void (i32, ...) @__bcdb_id_[[ID0]](i32 %0, ...)

; CHECK: attributes #0 = { noinline optnone }

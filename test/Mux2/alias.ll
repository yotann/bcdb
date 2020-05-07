; RUN: bcdb init -uri sqlite:%t.bcdb
; RUN: bcdb add -uri sqlite:%t.bcdb %s -name prog
; RUN: bcdb mux2 -uri sqlite:%t.bcdb prog -o %t --muxed-name=libmuxed.so --weak-name=libweak.so
; RUN: opt -verify -S < %t/libmuxed.so | FileCheck --check-prefix=MUXED %s
; RUN: opt -verify -S < %t/prog        | FileCheck --check-prefix=PROG  %s
; RUN: opt -verify -S < %t/libweak.so  | FileCheck --check-prefix=WEAK  %s

@alias = alias void (), void ()* @target

define void @target() {
  call void @alias()
  ret void
}

; MUXED: define protected void @__bcdb_body_target()
; MUXED-NEXT: call void @alias()
; MUXED: declare extern_weak void @alias()

; PROG: @alias = alias void (), void ()* @target
; PROG: declare void @__bcdb_body_target()
; PROG: define void @target()
; PROG-NEXT: call void @__bcdb_body_target()

; WEAK: define weak void @alias()

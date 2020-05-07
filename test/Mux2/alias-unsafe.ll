; REQUIRES: llvm10

; RUN: bcdb init -uri sqlite:%t.bcdb
; RUN: bcdb add -uri sqlite:%t.bcdb %s -name prog
; RUN: bcdb mux2 -uri sqlite:%t.bcdb prog -o %t --muxed-name=libmuxed.so --weak-name=libweak.so --allow-spurious-exports --known-dynamic-defs --known-dynamic-uses
; RUN: opt -verify -S < %t/libmuxed.so | FileCheck --check-prefix=MUXED %s
; RUN: opt -verify -S < %t/prog        | FileCheck --check-prefix=PROG  %s
; RUN: opt -verify -S < %t/libweak.so  | FileCheck --check-prefix=WEAK  %s

@alias1 = weak_odr alias void (), void ()* @target1
@alias2 = weak_odr alias void (), void ()* @target2

define weak_odr void @target1() {
  call void @alias1()
  ret void
}

define weak_odr void @target2() {
  ret void
}

; MUXED: @alias1 = weak_odr alias void (), void ()* @target1
; MUXED: @alias2 = weak_odr alias void (), void ()* @target2
; MUXED: define internal void @__bcdb_body_target1()
; MUXED-NEXT: call void @alias1()
; MUXED: define internal void @target1()
; MUXED-NEXT: call void @__bcdb_body_target1()
; MUXED: define internal void @__bcdb_body_target2()
; MUXED-NEXT: ret void
; MUXED: define internal void @target2()
; MUXED-NEXT: call void @__bcdb_body_target2()

; PROG-NOT: @alias1
; PROG-NOT: @target1
; PROG-NOT: @alias2
; PROG-NOT: @target2

; WEAK-NOT: @alias1
; WEAK-NOT: @target1
; WEAK-NOT: @alias2
; WEAK-NOT: @target2

; RUN: bcdb init -uri sqlite:%t.bcdb
; RUN: bcdb add -uri sqlite:%t.bcdb %s -name prog
; RUN: bcdb mux2 -uri sqlite:%t.bcdb prog -o %t --muxed-name=libmuxed.so
; RUN: opt -verify -S < %t/libmuxed.so | FileCheck --check-prefix=MUXED %s
; RUN: opt -verify -S < %t/prog        | FileCheck --check-prefix=PROG  %s

@pointer = constant i8* @internal
@internal = internal global i8 0

define i8** @get_pointer() {
  ret i8** @pointer
}

define i8* @get_internal() {
  ret i8* @internal
}

; PROG: @pointer = constant i8* @__bcdb_private_internal
; PROG: @__bcdb_private_internal = global i8 0
; MUXED: @pointer = external constant i8*
; MUXED: @__bcdb_private_internal = external global i8
; MUXED: ret i8** @pointer
; MUXED: ret i8* @__bcdb_private_internal

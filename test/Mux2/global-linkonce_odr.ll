; RUN: bcdb init -uri sqlite:%t.bcdb
; RUN: bcdb add -uri sqlite:%t.bcdb %s -name prog
; RUN: bcdb mux2 -uri sqlite:%t.bcdb prog -o %t --muxed-name=libmuxed.so
; RUN: opt -verify -S < %t/libmuxed.so | FileCheck --check-prefix=MUXED %s
; RUN: opt -verify -S < %t/prog        | FileCheck --check-prefix=STUB  %s

$x = comdat any
@x = linkonce_odr constant i8 0, comdat

define i8* @f() {
  ret i8* @x
}

; MUXED: @x = linkonce constant i8 0
; MUXED: define i8* @__bcdb_id_
; MUXED-NEXT: ret i8* @x

; STUB: @x = constant i8 0, comdat

; RUN: bcdb init -uri sqlite:%t.bcdb
; RUN: bcdb add -uri sqlite:%t.bcdb %s -name prog
; RUN: bcdb mux2 -uri sqlite:%t.bcdb prog -o %t --muxed-name=libmuxed.so --weak-name=libweak.so --allow-spurious-exports
; RUN: opt -verify -S < %t/libmuxed.so | FileCheck --check-prefix=MUXED %s
; RUN: opt -verify -S < %t/prog        | FileCheck --check-prefix=STUB  %s
; RUN: opt -verify -S < %t/libweak.so  | FileCheck --check-prefix=WEAK  %s

$x = comdat any
@x = linkonce_odr constant i8 1, comdat

define i8* @f() {
  ret i8* @x
}

; MUXED: @x = extern_weak constant i8
; MUXED: define internal i8* @__bcdb_id_
; MUXED-NEXT: ret i8* @x
; MUXED: define i8* @f()
; MUXED-NEXT: call i8* @__bcdb_id

; STUB: @x = constant i8 1, comdat
; STUB-NOT: @f

; WEAK-NOT: @x

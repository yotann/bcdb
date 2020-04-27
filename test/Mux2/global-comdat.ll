; RUN: bcdb init -uri sqlite:%t.bcdb
; RUN: bcdb add -uri sqlite:%t.bcdb %s -name prog
; RUN: bcdb mux2 -uri sqlite:%t.bcdb prog -o %t --muxed-name=libmuxed.so
; RUN: opt -verify -S < %t/libmuxed.so | FileCheck --check-prefix=MUXED %s
; RUN: opt -verify -S < %t/prog        | FileCheck --check-prefix=STUB  %s

$f = comdat any
define linkonce_odr void @f() comdat {
  ret void
}

define void @g() {
  call void @f()
  ret void
}

; MUXED: declare void @f()

; STUB: define weak_odr void @f() comdat

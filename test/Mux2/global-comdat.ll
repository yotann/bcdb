; RUN: bcdb init -uri sqlite:%t.bcdb
; RUN: bcdb add -uri sqlite:%t.bcdb %s -name prog
; RUN: bcdb mux2 -uri sqlite:%t.bcdb prog -o %t --muxed-name=libmuxed.so --weak-name=libweak.so --known-rtld-local
; RUN: opt -verify -S < %t/libmuxed.so | FileCheck --check-prefix=MUXED %s
; RUN: opt -verify -S < %t/prog        | FileCheck --check-prefix=STUB  %s
; RUN: opt -verify -S < %t/libweak.so  | FileCheck --check-prefix=WEAK  %s

$f = comdat any
define linkonce_odr void @f() comdat {
  ret void
}

define void @g() {
  call void @f()
  ret void
}

; MUXED: declare extern_weak void @f()

; STUB: define weak_odr void @f() #0 comdat

; WEAK: define weak void @f() {
; WEAK-NEXT: call void @__bcdb_weak_definition_called

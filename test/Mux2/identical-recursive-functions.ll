; RUN: bcdb init -uri sqlite:%t.bcdb
; RUN: bcdb add -uri sqlite:%t.bcdb %s -name f
; RUN: bcdb add -uri sqlite:%t.bcdb %p/Inputs/identical-recursive-functions.ll -name g
; RUN: bcdb mux2 -uri sqlite:%t.bcdb f g -o %t --muxed-name=libmuxed.so
; RUN: opt -verify -S < %t/libmuxed.so | FileCheck --check-prefix=MUXED %s
; RUN: opt -verify -S < %t/f           | FileCheck --check-prefix=F     %s
; RUN: opt -verify -S < %t/g           | FileCheck --check-prefix=G     %s

$f = comdat any

define linkonce_odr void @f() comdat {
  call void @f()
  ret void
}

; MUXED-DAG: define protected void @__bcdb_id_1()
; MUXED-DAG: call void @f()
; MUXED-DAG: define protected void @__bcdb_id_1.0()
; MUXED-DAG: call void @g()
; F: define weak_odr void @f() comdat
; F-NEXT: tail call void @__bcdb_id_{{.*}}()
; G: define weak_odr void @g() comdat
; G-NEXT: tail call void @__bcdb_id_{{.*}}()

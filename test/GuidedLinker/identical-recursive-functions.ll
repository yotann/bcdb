; RUN: bcdb init -store sqlite:%t.bcdb
; RUN: bcdb add -store sqlite:%t.bcdb %s -name f
; RUN: bcdb add -store sqlite:%t.bcdb %p/Inputs/identical-recursive-functions.ll -name g
; RUN: bcdb gl -store sqlite:%t.bcdb f g -o %t --merged-name=libmerged.so --noplugin
; RUN: opt -verify -S < %t/libmerged.so | FileCheck --check-prefix=MERGED %s
; RUN: opt -verify -S < %t/f           | FileCheck --check-prefix=F     %s
; RUN: opt -verify -S < %t/g           | FileCheck --check-prefix=G     %s

$f = comdat any

define weak_odr void @f() comdat {
  call void @f()
  ret void
}

; MERGED-DAG: define protected void @__bcdb_body_f()
; MERGED-DAG: call void @f()
; MERGED-DAG: define protected void @__bcdb_body_g()
; MERGED-DAG: call void @g()
; F: define weak_odr void @f() #0 comdat
; F-NEXT: tail call void @__bcdb_body_f()
; G: define weak_odr void @g() #0 comdat
; G-NEXT: tail call void @__bcdb_body_g()

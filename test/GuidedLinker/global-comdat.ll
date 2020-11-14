; RUN: bcdb init -uri sqlite:%t.bcdb
; RUN: bcdb add -uri sqlite:%t.bcdb %s -name prog
; RUN: bcdb gl -uri sqlite:%t.bcdb prog -o %t --merged-name=libmerged.so --weak-name=libweak.so --noplugin
; RUN: opt -verify -S < %t/libmerged.so | FileCheck --check-prefix=MERGED %s
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

; MERGED: declare extern_weak void @f()

; STUB: define weak_odr void @f() #0 comdat

; WEAK: define weak void @f() {
; WEAK-NEXT: call void @__bcdb_weak_definition_called

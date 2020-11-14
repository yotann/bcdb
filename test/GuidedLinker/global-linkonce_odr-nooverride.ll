; RUN: bcdb init -uri sqlite:%t.bcdb
; RUN: bcdb add -uri sqlite:%t.bcdb %s -name prog
; RUN: bcdb gl -uri sqlite:%t.bcdb prog -o %t --merged-name=libmerged.so --weak-name=libweak.so --nooverride --noplugin
; RUN: opt -verify -S < %t/libmerged.so | FileCheck --check-prefix=MERGED %s
; RUN: opt -verify -S < %t/prog        | FileCheck --check-prefix=STUB  %s

$x = comdat any
@x = linkonce_odr constant i8 1, comdat

define i8* @f() {
  ret i8* @x
}

; MERGED: @x = extern_weak constant i8
; MERGED: define protected i8* @__bcdb_body_f()
; MERGED-NEXT: ret i8* @x

; STUB: @x = weak_odr constant i8 1, comdat

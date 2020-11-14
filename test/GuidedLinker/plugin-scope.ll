; RUN: bcdb init -uri sqlite:%t.bcdb
; RUN: bcdb add -uri sqlite:%t.bcdb %s -name prog
; RUN: bcdb gl -uri sqlite:%t.bcdb prog -o %t --merged-name=libmerged.so --noweak
; RUN: opt -verify -S < %t/libmerged.so | FileCheck --check-prefix=MERGED %s
; RUN: opt -verify -S < %t/prog        | FileCheck --check-prefix=STUB  %s

@global = weak global i32 ()* @func

define weak i32 @func() {
  ret i32 -1000
}

; MERGED-NOT: @global
; MERGED-NOT: @func
; MERGED: define protected i32 @__bcdb_body_func()
; MERGED-NEXT: ret i32 -1000

; STUB: @global = weak global i32 ()* @func
; STUB: declare i32 @__bcdb_body_func()
; STUB: define weak i32 @func()
; STUB-NEXT: call i32 @__bcdb_body_func()

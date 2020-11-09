; RUN: bcdb init -uri sqlite:%t.bcdb
; RUN: bcdb add -uri sqlite:%t.bcdb %s -name prog
; RUN: bcdb gl -uri sqlite:%t.bcdb prog -o %t --muxed-name=libmuxed.so --noweak
; RUN: opt -verify -S < %t/libmuxed.so | FileCheck --check-prefix=MUXED %s
; RUN: opt -verify -S < %t/prog        | FileCheck --check-prefix=STUB  %s

@global = weak global i32 ()* @func

define weak i32 @func() {
  ret i32 -1000
}

; MUXED-NOT: @global
; MUXED-NOT: @func
; MUXED: define protected i32 @__bcdb_body_func()
; MUXED-NEXT: ret i32 -1000

; STUB: @global = weak global i32 ()* @func
; STUB: declare i32 @__bcdb_body_func()
; STUB: define weak i32 @func()
; STUB-NEXT: call i32 @__bcdb_body_func()

; RUN: bcdb init -uri sqlite:%t.bcdb
; RUN: bcdb add -uri sqlite:%t.bcdb %s -name prog
; RUN: bcdb mux2 -uri sqlite:%t.bcdb prog -o %t --muxed-name=libmuxed.so
; RUN: opt -verify -S < %t/libmuxed.so | FileCheck --check-prefix=MUXED %s
; RUN: opt -verify -S < %t/prog        | FileCheck --check-prefix=STUB  %s

define internal void @foo() {
  ret void
}

define void @bar() {
  call void @foo()
  ret void
}

; MUXED: define internal void @__bcdb_id_1()
; MUXED: define internal void @foo()
; MUXED-NEXT: tail call void @__bcdb_id_1()
; MUXED: define protected void @__bcdb_id_2()
; MUXED-NEXT: call void @foo()

; STUB: declare void @__bcdb_id_2()
; STUB: define void @bar()
; STUB-NEXT: tail call void @__bcdb_id_2()

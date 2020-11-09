; RUN: bcdb init -uri sqlite:%t.bcdb
; RUN: bcdb add -uri sqlite:%t.bcdb %s -name prog
; RUN: bcdb gl -uri sqlite:%t.bcdb prog -o %t --muxed-name=libmuxed.so
; RUN: opt -verify -S < %t/libmuxed.so | FileCheck --check-prefix=MUXED %s
; RUN: opt -verify -S < %t/prog        | FileCheck --check-prefix=STUB  %s

define internal void @foo() {
  ret void
}

define void @bar() {
  call void @foo()
  ret void
}

; MUXED: define internal void @__bcdb_body_foo()
; MUXED: define protected void @__bcdb_body_bar()
; MUXED-NEXT: call void @__bcdb_body_foo()

; STUB: declare void @__bcdb_body_bar()
; STUB: define void @bar()
; STUB-NEXT: tail call void @__bcdb_body_bar()

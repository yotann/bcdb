; RUN: memodb init -store sqlite:%t.bcdb
; RUN: bcdb add -store sqlite:%t.bcdb %s -name prog
; RUN: bcdb gl -store sqlite:%t.bcdb prog -o %t --merged-name=libmerged.so
; RUN: opt -verify -S < %t/libmerged.so | FileCheck --check-prefix=MERGED %s
; RUN: opt -verify -S < %t/prog        | FileCheck --check-prefix=STUB  %s

define internal void @foo() {
  ret void
}

define void @bar() {
  call void @foo()
  ret void
}

; MERGED: define internal void @__bcdb_body_foo()
; MERGED: define protected void @__bcdb_body_bar()
; MERGED-NEXT: call void @__bcdb_body_foo()

; STUB: declare void @__bcdb_body_bar()
; STUB: define void @bar()
; STUB-NEXT: tail call void @__bcdb_body_bar()

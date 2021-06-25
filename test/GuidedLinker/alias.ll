; RUN: bcdb init -store sqlite:%t.bcdb
; RUN: bcdb add -store sqlite:%t.bcdb %s -name prog
; RUN: bcdb gl -store sqlite:%t.bcdb prog -o %t --merged-name=libmerged.so --weak-name=libweak.so --noplugin
; RUN: opt -verify -S < %t/libmerged.so | FileCheck --check-prefix=MERGED %s
; RUN: opt -verify -S < %t/prog        | FileCheck --check-prefix=PROG  %s
; RUN: opt -verify -S < %t/libweak.so  | FileCheck --check-prefix=WEAK  %s

@alias = alias void (), void ()* @target

define void @target() {
  call void @alias()
  ret void
}

; MERGED: define protected void @__bcdb_body_target()
; MERGED-NEXT: call void @alias()
; MERGED: declare extern_weak void @alias()

; PROG: @alias = alias void (), void ()* @target
; PROG: declare void @__bcdb_body_target()
; PROG: define void @target()
; PROG-NEXT: call void @__bcdb_body_target()

; WEAK: define weak void @alias()

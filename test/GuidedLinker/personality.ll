; RUN: bcdb init -uri sqlite:%t.bcdb
; RUN: bcdb add -uri sqlite:%t.bcdb %s -name mod
; RUN: bcdb gl -uri sqlite:%t.bcdb mod -o %t --merged-name=libmerged.so
; RUN: opt -verify -S < %t/libmerged.so | FileCheck %s

declare i32 @__gxx_personality_v0(...)

; CHECK: define protected void @__bcdb_body_foo() personality i8* bitcast (i32 (...)* @__gxx_personality_v0 to i8*)

define void @foo() personality i8* bitcast (i32 (...)* @__gxx_personality_v0 to i8*) {
  ret void
}

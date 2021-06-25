; RUN: bcdb init -store sqlite:%t.bcdb
; RUN: bcdb add -store sqlite:%t.bcdb %s -name mod
; RUN: bcdb gl -store sqlite:%t.bcdb mod -o %t --merged-name=libmerged.so
; RUN: opt -verify -S < %t/libmerged.so | FileCheck %s

declare i32 @__gxx_personality_v0(...)

; CHECK: define protected void @__bcdb_body_foo() personality i8* bitcast (i32 (...)* @__gxx_personality_v0 to i8*)

define void @foo() personality i8* bitcast (i32 (...)* @__gxx_personality_v0 to i8*) {
  ret void
}

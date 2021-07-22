; RUN: memodb init -store sqlite:%t.bcdb
; RUN: bcdb add -store sqlite:%t.bcdb %s -name prog
; RUN: bcdb add -store sqlite:%t.bcdb %p/Inputs/exported-twice-and-directly-used.ll -name weak
; RUN: bcdb gl -store sqlite:%t.bcdb prog weak -o %t --merged-name=libmerged.so --weak-name=libweak.so --noplugin
; RUN: opt -verify -S < %t/libmerged.so | FileCheck --check-prefix=MERGED %s
; RUN: opt -verify -S < %t/prog        | FileCheck --check-prefix=PROG  %s
; RUN: opt -verify -S < %t/libweak.so  | FileCheck --check-prefix=WEAK  %s

@exported_constant = constant i32 -12

define i32 @exported_func() {
  ret i32 12
}

define i32 @user() {
  call i32 @exported_func()
  %x = load i32, i32* @exported_constant
  ret i32 %x
}

; MERGED: define protected i32 @__bcdb_body_exported_func()
; MERGED-NEXT: ret i32 12
; MERGED: define protected i32 @__bcdb_body_user()
; MERGED-NEXT: call i32 @__bcdb_body_exported_func()
; MERGED-NEXT: ret i32 -12

; PROG: @exported_constant = constant i32 -12
; PROG: @__bcdb_direct_exported_constant = alias i32, i32* @exported_constant
; PROG: define i32 @exported_func()
; PROG-NEXT: %1 = tail call i32 @__bcdb_body_exported_func()
; PROG: define i32 @user()
; PROG-NEXT: call i32 @__bcdb_body_user()

; WEAK: define weak i32 @exported_func()
; WEAK-NEXT: call void @__bcdb_weak_definition_called

; RUN: bcdb init -uri sqlite:%t.bcdb
; RUN: bcdb add -uri sqlite:%t.bcdb %s -name prog
; RUN: bcdb add -uri sqlite:%t.bcdb %p/Inputs/exported-once-and-directly-used.ll -name private
; RUN: bcdb gl -uri sqlite:%t.bcdb prog private -o %t --merged-name=libmerged.so --weak-name=libweak.so --noplugin
; RUN: opt -verify -S < %t/libmerged.so | FileCheck --check-prefix=MERGED %s
; RUN: opt -verify -S < %t/prog        | FileCheck --check-prefix=PROG  %s

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
; PROG: define i32 @exported_func()
; PROG-NEXT: %1 = tail call i32 @__bcdb_body_exported_func()
; PROG: define i32 @user()
; PROG-NEXT: call i32 @__bcdb_body_user()

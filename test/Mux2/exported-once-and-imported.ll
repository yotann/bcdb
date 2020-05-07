; RUN: bcdb init -uri sqlite:%t.bcdb
; RUN: bcdb add -uri sqlite:%t.bcdb %s -name prog
; RUN: bcdb add -uri sqlite:%t.bcdb %p/Inputs/exported-once-and-imported.ll -name import
; RUN: bcdb mux2 -uri sqlite:%t.bcdb prog import -o %t --muxed-name=libmuxed.so --weak-name=libweak.so --known-rtld-local
; RUN: opt -verify -S < %t/libmuxed.so | FileCheck --check-prefix=MUXED %s
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

; MUXED: @exported_constant = extern_weak constant i32
; MUXED: define protected i32 @__bcdb_body_exported_func()
; MUXED-NEXT: ret i32 12
; MUXED: define protected i32 @__bcdb_body_user()
; MUXED-NEXT: call i32 @__bcdb_body_exported_func()
; MUXED-NEXT: ret i32 -12

; PROG: @exported_constant = constant i32 -12
; PROG: @__bcdb_direct_exported_constant = alias i32, i32* @exported_constant
; PROG: define i32 @exported_func()
; PROG-NEXT: %1 = tail call i32 @__bcdb_body_exported_func()
; PROG: define i32 @user()
; PROG-NEXT: call i32 @__bcdb_body_user()

; WEAK: define weak i32 @exported_func()
; WEAK-NEXT: call void @__bcdb_weak_definition_called

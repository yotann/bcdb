; RUN: bcdb init -uri sqlite:%t.bcdb
; RUN: bcdb add -uri sqlite:%t.bcdb %s -name prog
; RUN: bcdb gl -uri sqlite:%t.bcdb prog -o %t --merged-name=libmerged.so --noplugin
; RUN: opt -verify -S < %t/libmerged.so | FileCheck --check-prefix=MERGED %s
; RUN: opt -verify -S < %t/prog        | FileCheck --check-prefix=PROG  %s

@pointer = constant i8* @internal
@internal = internal global i8 11

define i8** @get_pointer() {
  ret i8** @pointer
}

define i8* @get_internal() {
  ret i8* @internal
}

; PROG: @pointer = constant i8* @__bcdb_merged_internal
; PROG: @__bcdb_merged_internal = available_externally global i8 11
; MERGED: @pointer = extern_weak constant i8*
; MERGED: @__bcdb_merged_internal = global i8 11
; MERGED: ret i8** @pointer
; MERGED: ret i8* @__bcdb_merged_internal

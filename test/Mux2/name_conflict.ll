; RUN: bcdb init -uri sqlite:%t.bcdb
; RUN: bcdb add -uri sqlite:%t.bcdb %s -name rogue
; RUN: bcdb add -uri sqlite:%t.bcdb %p/Inputs/name_conflict.ll -name angband
; RUN: bcdb mux2 -uri sqlite:%t.bcdb rogue angband -o %t --muxed-name=libmuxed.so --weak-name=libweak.so
; RUN: opt -verify -S < %t/libmuxed.so | FileCheck --check-prefix=MUXED %s
; RUN: opt -verify -S < %t/rogue       | FileCheck --check-prefix=ROGUE %s
; RUN: opt -verify -S < %t/angband     | FileCheck --check-prefix=ANGBAND %s
; RUN: opt -verify -S < %t/libweak.so  | FileCheck --check-prefix=WEAK  %s

define i32 @init_player() {
  ret i32 1
}

define i32 @main() {
  %r = tail call i32 (...) bitcast (i32 ()* @init_player to i32 (...)*)()
  ret i32 %r
}

; ROGUE: define i32 @init_player()
; ROGUE-NEXT: tail call i32 @__bcdb_id_1()
; ROGUE: define i32 @main()
; ROGUE-NEXT: tail call i32 @__bcdb_id_2()
; MUXED: define i32 @__bcdb_id_1()
; MUXED-NEXT: ret i32 1
; MUXED: define i32 @__bcdb_id_2()
; MUXED-NEXT: tail call i32 (...) bitcast (i32 ()* @init_player to i32 (...)*)()
; MUXED: declare extern_weak i32 @init_player()

; ANGBAND: @player_module = global i32 ()* @init_player
; ANGBAND: define internal i32 @init_player()
; ANGBAND-NEXT: tail call i32 @__bcdb_id_5()
; MUXED: define i32 @__bcdb_id_5()
; MUXED-NEXT: ret i32 2

; WEAK: define weak i32 @init_player()
; WEAK-NEXT: call void @__bcdb_weak_definition_called

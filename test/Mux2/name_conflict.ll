; RUN: bcdb init -uri sqlite:%t.bcdb
; RUN: bcdb add -uri sqlite:%t.bcdb %s -name rogue
; RUN: bcdb add -uri sqlite:%t.bcdb %p/Inputs/name_conflict.ll -name angband
; RUN: bcdb mux2 -uri sqlite:%t.bcdb rogue angband -o %t --muxed-name=libmuxed.so --weak-name=libweak.so --known-rtld-local
; RUN: opt -verify -S < %t/libmuxed.so | FileCheck --check-prefix=MUXED %s
; RUN: opt -verify -S < %t/rogue       | FileCheck --check-prefix=ROGUE %s
; RUN: opt -verify -S < %t/angband     | FileCheck --check-prefix=ANGBAND %s

define i32 @init_player() {
  ret i32 1
}

define i32 @main() {
  %r = tail call i32 (...) bitcast (i32 ()* @init_player to i32 (...)*)()
  ret i32 %r
}

; MUXED: define protected i32 @__bcdb_body_init_player[[ID0:.*]]()
; MUXED-NEXT: ret i32 1
; MUXED: define protected i32 @__bcdb_body_main()
; MUXED-NEXT: tail call i32 @__bcdb_body_init_player[[ID0]]()
; MUXED: define internal i32 @__bcdb_body_init_player[[ID1:.*]]()
; MUXED-NEXT: ret i32 2
; MUXED: define protected i32 @__bcdb_merged_init_player()
; MUXED-NEXT: tail call i32 @__bcdb_body_init_player[[ID1]]()

; ROGUE: define i32 @init_player()
; ROGUE-NEXT: tail call i32 @__bcdb_body_init_player[[ID0:.*]]()
; ROGUE: define i32 @main()
; ROGUE-NEXT: tail call i32 @__bcdb_body_main()

; ANGBAND-NOT: @init_player
; ANGBAND: @player_module = global i32 ()* @__bcdb_merged_init_player
; ANGBAND: declare i32 @__bcdb_merged_init_player()

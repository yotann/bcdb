; RUN: memodb init -store sqlite:%t.bcdb
; RUN: bcdb add -store sqlite:%t.bcdb %s -name rogue
; RUN: bcdb add -store sqlite:%t.bcdb %p/Inputs/name_conflict.ll -name angband
; RUN: bcdb gl -store sqlite:%t.bcdb rogue angband -o %t --merged-name=libmerged.so --weak-name=libweak.so --noplugin
; RUN: opt -verify -S < %t/libmerged.so | FileCheck --check-prefix=MERGED %s
; RUN: opt -verify -S < %t/rogue       | FileCheck --check-prefix=ROGUE %s
; RUN: opt -verify -S < %t/angband     | FileCheck --check-prefix=ANGBAND %s

define i32 @init_player() {
  ret i32 1
}

define i32 @main() {
  %r = tail call i32 (...) bitcast (i32 ()* @init_player to i32 (...)*)()
  ret i32 %r
}

; MERGED: define protected i32 @__bcdb_body_init_player[[ID0:.*]]()
; MERGED-NEXT: ret i32 1
; MERGED: define protected i32 @__bcdb_body_main()
; MERGED-NEXT: tail call i32 @__bcdb_body_init_player[[ID0]]()
; MERGED: define internal i32 @__bcdb_body_init_player[[ID1:.*]]()
; MERGED-NEXT: ret i32 2
; MERGED: define protected i32 @__bcdb_merged_init_player()
; MERGED-NEXT: tail call i32 @__bcdb_body_init_player[[ID1]]()

; ROGUE: define i32 @init_player()
; ROGUE-NEXT: tail call i32 @__bcdb_body_init_player[[ID0:.*]]()
; ROGUE: define i32 @main()
; ROGUE-NEXT: tail call i32 @__bcdb_body_main()

; ANGBAND-NOT: @init_player
; ANGBAND: @player_module = global i32 ()* @__bcdb_merged_init_player
; ANGBAND: declare i32 @__bcdb_merged_init_player()

; RUN: rm -r %t
; RUN: bcdb init -uri leveldb:%t
; RUN: llvm-as < %s | bcdb add -uri leveldb:%t -name a -
; RUN: llvm-as < %s | bcdb add -uri leveldb:%t -name a -
; RUN: llvm-as < %s | bcdb add -uri leveldb:%t -name b -
; RUN: bcdb list-modules -uri leveldb:%t | sort | FileCheck --check-prefix=MODS %s
; RUN: bcdb list-function-ids -uri leveldb:%t | wc -l | FileCheck --check-prefix=IDS %s

; MODS: a
; MODS-NEXT: b

; IDS: 1

define void @func() {
  ret void
}

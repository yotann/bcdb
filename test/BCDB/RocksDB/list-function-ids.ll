; RUN: rm -rf %t
; RUN: bcdb init -uri rocksdb:%t
; RUN: llvm-as < %s | bcdb add -uri rocksdb:%t -name a -
; RUN: llvm-as < %s | bcdb add -uri rocksdb:%t -name a -
; RUN: llvm-as < %s | bcdb add -uri rocksdb:%t -name b -
; RUN: bcdb list-modules -uri rocksdb:%t | sort | FileCheck --check-prefix=MODS %s
; RUN: bcdb list-function-ids -uri rocksdb:%t | wc -l | FileCheck --check-prefix=IDS %s

; MODS: a
; MODS-NEXT: b

; IDS: 1

define void @func() {
  ret void
}

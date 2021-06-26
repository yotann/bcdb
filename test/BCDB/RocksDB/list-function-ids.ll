; RUN: rm -rf %t
; RUN: memodb init -store rocksdb:%t
; RUN: llvm-as < %s | bcdb add -store rocksdb:%t -name a -
; RUN: llvm-as < %s | bcdb add -store rocksdb:%t -name a -
; RUN: llvm-as < %s | bcdb add -store rocksdb:%t -name b -
; RUN: bcdb list-modules -store rocksdb:%t | sort | FileCheck --check-prefix=MODS %s
; RUN: bcdb list-function-ids -store rocksdb:%t | wc -l | FileCheck --check-prefix=IDS %s

; MODS: a
; MODS-NEXT: b

; IDS: 1

define void @func() {
  ret void
}

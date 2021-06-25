; RUN: rm -rf %t
; RUN: bcdb init -store sqlite:%t
; RUN: llvm-as < %s | bcdb add -store sqlite:%t -name a -
; RUN: llvm-as < %s | bcdb add -store sqlite:%t -name a -
; RUN: llvm-as < %s | bcdb add -store sqlite:%t -name b -
; RUN: bcdb list-modules -store sqlite:%t | sort | FileCheck --check-prefix=MODS %s
; RUN: bcdb list-function-ids -store sqlite:%t | wc -l | FileCheck --check-prefix=IDS %s

; MODS: a
; MODS-NEXT: b

; IDS: 1

define void @func() {
  ret void
}

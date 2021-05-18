; RUN: bcdb init -uri sqlite:%t
; RUN: llvm-as < %s | bcdb add -uri sqlite:%t -name a -
; RUN: llvm-as < %s | bcdb add -uri sqlite:%t -name a -
; RUN: llvm-as < %s | bcdb add -uri sqlite:%t -name b -
; RUN: bcdb list-modules -uri sqlite:%t | sort | FileCheck --check-prefix=MODS %s
; RUN: bcdb list-function-ids -uri sqlite:%t | wc -l | FileCheck --check-prefix=IDS %s

; MODS: a
; MODS-NEXT: b

; IDS: 1

define void @func() {
  ret void
}

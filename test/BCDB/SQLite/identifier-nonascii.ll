; RUN: rm -rf %t
; RUN: bcdb init -uri sqlite:%t
; RUN: llvm-as < %s | bcdb add -uri sqlite:%t -
; RUN: bcdb get -uri sqlite:%t -name - | opt -verify -S | FileCheck %s
; RUN: bcdb get-function -uri sqlite:%t -id $(bcdb list-function-ids -uri sqlite:%t) | opt -verify -S | FileCheck --check-prefix=FUNC %s
; RUN: bcdb refs -uri sqlite:%t $(bcdb list-function-ids -uri sqlite:%t) | FileCheck --check-prefix=REFS %s

; FUNC: define void @0()
; CHECK: define void @"\01\FF\7F"()
define void @"\01\FF\7F"() {
  ret void
}

; REFS: heads["-"]["functions"]["\u0001ÿ\u007f"]

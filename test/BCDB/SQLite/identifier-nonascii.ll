; RUN: rm -rf %t
; RUN: bcdb init -store sqlite:%t
; RUN: llvm-as < %s | bcdb add -store sqlite:%t -
; RUN: bcdb get -store sqlite:%t -name - | opt -verify -S | FileCheck %s
; RUN: bcdb get-function -store sqlite:%t -id $(bcdb list-function-ids -store sqlite:%t) | opt -verify -S | FileCheck --check-prefix=FUNC %s
; RUN: bcdb refs -store sqlite:%t $(bcdb list-function-ids -store sqlite:%t) | FileCheck --check-prefix=REFS %s

; FUNC: define void @0()
; CHECK: define void @"\01\FF\7F"()
define void @"\01\FF\7F"() {
  ret void
}

; REFS: heads["-"]["functions"]["\u0001ÿ"]

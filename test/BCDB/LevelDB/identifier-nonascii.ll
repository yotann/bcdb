; RUN: rm -r %t
; RUN: bcdb init -uri leveldb:%t
; RUN: llvm-as < %s | bcdb add -uri leveldb:%t -
; RUN: bcdb get -uri leveldb:%t -name - | opt -verify -S | FileCheck %s
; RUN: bcdb get-function -uri leveldb:%t -id $(bcdb list-function-ids -uri leveldb:%t) | opt -verify -S | FileCheck --check-prefix=FUNC %s
; RUN: bcdb refs -uri leveldb:%t $(bcdb list-function-ids -uri leveldb:%t) | FileCheck --check-prefix=REFS %s

; FUNC: define void @0()
; CHECK: define void @"\01\FF\7F"()
define void @"\01\FF\7F"() {
  ret void
}

; REFS: heads["-"]["functions"][h'01ff7f']

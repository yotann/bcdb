; RUN: rm -rf %t
; RUN: bcdb init -uri rocksdb:%t
; RUN: llvm-as < %s | bcdb add -uri rocksdb:%t -
; RUN: bcdb get -uri rocksdb:%t -name - | opt -verify -S | FileCheck %s
; RUN: bcdb get-function -uri rocksdb:%t -id $(bcdb list-function-ids -uri rocksdb:%t) | opt -verify -S | FileCheck --check-prefix=FUNC %s
; RUN: bcdb refs -uri rocksdb:%t $(bcdb list-function-ids -uri rocksdb:%t) | FileCheck --check-prefix=REFS %s

; FUNC: define void @0()
; CHECK: define void @"\01\FF\7F"()
define void @"\01\FF\7F"() {
  ret void
}

; REFS: heads["-"]["functions"]["\u0001ÿ\u007f"]

; RUN: rm -rf %t
; RUN: bcdb init -store rocksdb:%t
; RUN: llvm-as < %s | bcdb add -store rocksdb:%t -
; RUN: bcdb get -store rocksdb:%t -name - | opt -verify -S | FileCheck %s
; RUN: bcdb get-function -store rocksdb:%t -id $(bcdb list-function-ids -store rocksdb:%t) | opt -verify -S | FileCheck --check-prefix=FUNC %s
; RUN: bcdb refs -store rocksdb:%t $(bcdb list-function-ids -store rocksdb:%t) | FileCheck --check-prefix=REFS %s

; FUNC: define void @0()
; CHECK: define void @"\01\FF\7F"()
define void @"\01\FF\7F"() {
  ret void
}

; REFS: heads["-"]["functions"]["\u0001ÿ"]

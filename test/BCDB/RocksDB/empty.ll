; RUN: rm -rf %t
; RUN: bcdb init -store rocksdb:%t
; RUN: llvm-as < %s | bcdb add -store rocksdb:%t -
; RUN: bcdb get -store rocksdb:%t -name - | opt -verify -S

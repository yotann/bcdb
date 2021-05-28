; RUN: rm -rf %t
; RUN: bcdb init -uri rocksdb:%t
; RUN: llvm-as < %s | bcdb add -uri rocksdb:%t -
; RUN: bcdb get -uri rocksdb:%t -name - | opt -verify -S

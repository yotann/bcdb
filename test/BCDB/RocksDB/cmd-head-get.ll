; RUN: rm -rf %t
; RUN: bcdb init     -store rocksdb:%t
; RUN: bcdb add      -store rocksdb:%t %s -name x
; RUN: bcdb add      -store rocksdb:%t %s -name y
; RUN: bcdb head-get -store rocksdb:%t x y | FileCheck %s

; CHECK: [[ID:[0-9A-Za-z]+]]
; CHECK-NEXT: [[ID]]

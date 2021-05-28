; RUN: rm -rf %t
; RUN: bcdb init     -uri rocksdb:%t
; RUN: bcdb add      -uri rocksdb:%t %s -name x
; RUN: bcdb add      -uri rocksdb:%t %s -name y
; RUN: bcdb head-get -uri rocksdb:%t x y | FileCheck %s

; CHECK: [[ID:[0-9A-Za-z]+]]
; CHECK-NEXT: [[ID]]

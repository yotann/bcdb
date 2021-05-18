; RUN: rm -r %t
; RUN: bcdb init     -uri leveldb:%t
; RUN: bcdb add      -uri leveldb:%t %s -name x
; RUN: bcdb add      -uri leveldb:%t %s -name y
; RUN: bcdb head-get -uri leveldb:%t x y | FileCheck %s

; CHECK: [[ID:[A-Za-z0-9+/=]+]]
; CHECK-NEXT: [[ID]]

; RUN: rm -rf %t
; RUN: bcdb init     -store sqlite:%t
; RUN: bcdb add      -store sqlite:%t %s -name x
; RUN: bcdb add      -store sqlite:%t %s -name y
; RUN: bcdb head-get -store sqlite:%t x y | FileCheck %s

; CHECK: [[ID:[0-9A-Za-z]+]]
; CHECK-NEXT: [[ID]]

; RUN: bcdb init     -uri sqlite:%t
; RUN: bcdb add      -uri sqlite:%t %s -name x
; RUN: bcdb add      -uri sqlite:%t %s -name y
; RUN: bcdb head-get -uri sqlite:%t x y | FileCheck %s

; CHECK: 2
; CHECK-NEXT: 2

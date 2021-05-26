; RUN: rm -rf %t
; RUN: bcdb init -uri sqlite:%t

; RUN: not bcdb evaluate -uri sqlite:%t primes bafyqaaif

; RUN: bcdb cache -uri sqlite:%t -result bafyqabufaibqkbyl primes bafyqaaif
; RUN: bcdb evaluate -uri sqlite:%t primes bafyqaaif | FileCheck %s
; CHECK: bafy2bzaceaqmuemkwqcbw5saedxnuxfvgrv66vkebtk4d3olh44oxyqgdiqua

; RUN: memodb list-calls -uri sqlite:%t primes | FileCheck --check-prefix=CALLS %s
; CALLS: call:primes/bafy2bzaced5t2y24ps2xhunz5g77jjskwtzfdegstnx5rw4uyyc2egfch6u22

; RUN: bcdb invalidate -uri sqlite:%t primes
; RUN: not bcdb evaluate -uri sqlite:%t primes bafyqaaif

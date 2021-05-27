; RUN: rm -rf %t
; RUN: bcdb init -uri sqlite:%t

; RUN: not bcdb evaluate -uri sqlite:%t primes bafyqaaif

; RUN: bcdb cache -uri sqlite:%t -result bafyqabufaibqkbyl primes bafyqaaif
; RUN: bcdb evaluate -uri sqlite:%t primes bafyqaaif | FileCheck %s
; CHECK: bafyqabufaibqkbyl

; RUN: memodb list-calls -uri sqlite:%t primes | FileCheck --check-prefix=CALLS %s
; CALLS: call:primes/bafyqaaif

; RUN: bcdb invalidate -uri sqlite:%t primes
; RUN: not bcdb evaluate -uri sqlite:%t primes bafyqaaif

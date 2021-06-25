; RUN: rm -rf %t
; RUN: bcdb init -store sqlite:%t

; RUN: not bcdb evaluate -store sqlite:%t primes bafyqaaif

; RUN: bcdb cache -store sqlite:%t -result bafyqabufaibqkbyl primes bafyqaaif
; RUN: bcdb evaluate -store sqlite:%t primes bafyqaaif | FileCheck %s
; CHECK: bafyqabufaibqkbyl

; RUN: memodb list-calls -store sqlite:%t primes | FileCheck --check-prefix=CALLS %s
; CALLS: call:primes/bafyqaaif

; RUN: bcdb invalidate -store sqlite:%t primes
; RUN: not bcdb evaluate -store sqlite:%t primes bafyqaaif

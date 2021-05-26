; RUN: rm -rf %t
; RUN: bcdb init -uri leveldb:%t

; RUN: not bcdb evaluate -uri leveldb:%t primes bafyqaaif

; RUN: bcdb cache -uri leveldb:%t -result bafyqabufaibqkbyl primes bafyqaaif
; RUN: bcdb evaluate -uri leveldb:%t primes bafyqaaif | FileCheck %s
; CHECK: bafyqabufaibqkbyl

; RUN: memodb list-calls -uri leveldb:%t primes | FileCheck --check-prefix=CALLS %s
; CALLS: call:primes/bafyqaaif

; RUN: bcdb invalidate -uri leveldb:%t primes
; RUN: not bcdb evaluate -uri leveldb:%t primes bafyqaaif

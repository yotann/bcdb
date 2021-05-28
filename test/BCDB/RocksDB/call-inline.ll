; RUN: rm -rf %t
; RUN: bcdb init -uri rocksdb:%t

; RUN: not bcdb evaluate -uri rocksdb:%t primes bafyqaaif

; RUN: bcdb cache -uri rocksdb:%t -result bafyqabufaibqkbyl primes bafyqaaif
; RUN: bcdb evaluate -uri rocksdb:%t primes bafyqaaif | FileCheck %s
; CHECK: bafyqabufaibqkbyl

; RUN: memodb list-calls -uri rocksdb:%t primes | FileCheck --check-prefix=CALLS %s
; CALLS: call:primes/bafyqaaif

; RUN: bcdb invalidate -uri rocksdb:%t primes
; RUN: not bcdb evaluate -uri rocksdb:%t primes bafyqaaif

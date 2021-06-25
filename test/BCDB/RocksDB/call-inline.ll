; RUN: rm -rf %t
; RUN: bcdb init -store rocksdb:%t

; RUN: not bcdb evaluate -store rocksdb:%t primes bafyqaaif

; RUN: bcdb cache -store rocksdb:%t -result bafyqabufaibqkbyl primes bafyqaaif
; RUN: bcdb evaluate -store rocksdb:%t primes bafyqaaif | FileCheck %s
; CHECK: bafyqabufaibqkbyl

; RUN: memodb list-calls -store rocksdb:%t primes | FileCheck --check-prefix=CALLS %s
; CALLS: call:primes/bafyqaaif

; RUN: bcdb invalidate -store rocksdb:%t primes
; RUN: not bcdb evaluate -store rocksdb:%t primes bafyqaaif

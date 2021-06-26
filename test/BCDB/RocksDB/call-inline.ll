; RUN: rm -rf %t
; RUN: memodb init -store rocksdb:%t

; RUN: not memodb put -store rocksdb:%t call:primes/bafyqaaif

; RUN: memodb set -store rocksdb:%t call:primes/bafyqaaif id:bafyqabufaibqkbyl
; RUN: memodb put -store rocksdb:%t call:primes/bafyqaaif | FileCheck %s
; CHECK: bafyqabufaibqkbyl

; RUN: memodb list-calls -store rocksdb:%t primes | FileCheck --check-prefix=CALLS %s
; CALLS: call:primes/bafyqaaif

; RUN: memodb invalidate -store rocksdb:%t primes
; RUN: not memodb put -store rocksdb:%t call:primes/bafyqaaif

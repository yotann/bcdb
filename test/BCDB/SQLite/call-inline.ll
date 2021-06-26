; RUN: rm -rf %t
; RUN: memodb init -store sqlite:%t

; RUN: not memodb put -store sqlite:%t call:primes/bafyqaaif

; RUN: memodb set -store sqlite:%t call:primes/bafyqaaif id:bafyqabufaibqkbyl
; RUN: memodb put -store sqlite:%t call:primes/bafyqaaif | FileCheck %s
; CHECK: bafyqabufaibqkbyl

; RUN: memodb list-calls -store sqlite:%t primes | FileCheck --check-prefix=CALLS %s
; CALLS: call:primes/bafyqaaif

; RUN: memodb invalidate -store sqlite:%t primes
; RUN: not memodb put -store sqlite:%t call:primes/bafyqaaif

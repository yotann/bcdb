; RUN: rm -rf %t
; RUN: memodb init -store sqlite:%t

; RUN: not memodb put -store sqlite:%t call:primes/uAXEAAQU

; RUN: memodb set -store sqlite:%t call:primes/uAXEAAQU id:uAXEABoUCAwUHCw
; RUN: memodb put -store sqlite:%t call:primes/uAXEAAQU | FileCheck %s
; CHECK: uAXEABoUCAwUHCw

; RUN: memodb list-calls -store sqlite:%t primes | FileCheck --check-prefix=CALLS %s
; CALLS: call:primes/uAXEAAQU

; RUN: memodb invalidate -store sqlite:%t primes
; RUN: not memodb put -store sqlite:%t call:primes/uAXEAAQU

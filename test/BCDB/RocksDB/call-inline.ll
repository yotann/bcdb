; RUN: rm -rf %t
; RUN: memodb init -store rocksdb:%t

; RUN: not memodb put -store rocksdb:%t /call/primes/uAXEAAQU

; RUN: memodb set -store rocksdb:%t /call/primes/uAXEAAQU /cid/uAXEABoUCAwUHCw
; RUN: memodb put -store rocksdb:%t /call/primes/uAXEAAQU | FileCheck %s
; CHECK: /cid/uAXEABoUCAwUHCw

; RUN: memodb list-calls -store rocksdb:%t primes | FileCheck --check-prefix=CALLS %s
; CALLS: /call/primes/uAXEAAQU

; RUN: memodb invalidate -store rocksdb:%t primes
; RUN: not memodb put -store rocksdb:%t /call/primes/uAXEAAQU

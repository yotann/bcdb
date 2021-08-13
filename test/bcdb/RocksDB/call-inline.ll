; RUN: rm -rf %t
; RUN: memodb init -store rocksdb:%t

; RUN: not memodb get -store rocksdb:%t /call/primes/uAXEAAQU

; RUN: memodb set -store rocksdb:%t /call/primes/uAXEAAQU /cid/uAXEABoUCAwUHCw
; RUN: memodb get -store rocksdb:%t /call/primes/uAXEAAQU | FileCheck %s
; CHECK: /cid/uAXEABoUCAwUHCw

; RUN: memodb get /call/primes -store rocksdb:%t | FileCheck --check-prefix=CALLS %s
; CALLS: /call/primes/uAXEAAQU

; RUN: memodb delete /call/primes -store rocksdb:%t
; RUN: not memodb get -store rocksdb:%t /call/primes/uAXEAAQU

; RUN: rm -rf %t
; RUN: memodb init -store sqlite:%t

; RUN: not memodb get -store sqlite:%t /call/primes/uAXEAAQU

; RUN: memodb set -store sqlite:%t /call/primes/uAXEAAQU /cid/uAXEABoUCAwUHCw
; RUN: memodb get -store sqlite:%t /call/primes/uAXEAAQU | FileCheck %s
; CHECK: /cid/uAXEABoUCAwUHCw

; RUN: memodb get /call/primes -store sqlite:%t | FileCheck --check-prefix=CALLS %s
; CALLS: /call/primes/uAXEAAQU

; RUN: memodb delete /call/primes -store sqlite:%t
; RUN: not memodb get -store sqlite:%t /call/primes/uAXEAAQU

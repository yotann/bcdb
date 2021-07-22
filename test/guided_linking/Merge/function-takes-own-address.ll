; RUN: memodb init -store sqlite:%t
; RUN: llvm-as < %s | bcdb add -store sqlite:%t -
; RUN: bcdb merge -store sqlite:%t - | lli

; RUN: memodb init -store sqlite:%t.rg
; RUN: llvm-as < %s | bcdb add -rename-globals -store sqlite:%t.rg -
; RUN: bcdb merge -store sqlite:%t.rg - | lli

; The value of @main in @pointer_to_main must equal the value of @main in @main
; itself, regardless of stubs. Something similar happens in OpenSSL's
; CRYPTO_malloc.

@pointer_to_main = internal global i32 (i32, i8**)* @main

define i32 @main(i32, i8**) {
  %3 = load i32 (i32, i8**)*, i32 (i32, i8**)** @pointer_to_main
  %4 = icmp ne i32 (i32, i8**)* %3, @main
  %5 = zext i1 %4 to i32
  ret i32 %5
}

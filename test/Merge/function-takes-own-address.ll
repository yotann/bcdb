; RUN: bcdb init -uri sqlite:%t
; RUN: llvm-as < %s | bcdb add -uri sqlite:%t -
; RUN: bcdb merge -uri sqlite:%t - | lli

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

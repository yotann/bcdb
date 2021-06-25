; RUN: bcdb init -store sqlite:%t
; RUN: llvm-as < %s | bcdb add -store sqlite:%t - -name a
; RUN: llvm-as < %s | bcdb add -store sqlite:%t - -name b
; RUN: bcdb merge -store sqlite:%t a b | opt -verify -S > %t.ll
; RUN: FileCheck --check-prefix=FUNC %s < %t.ll
; RUN: FileCheck --check-prefix=CALLER %s < %t.ll

define i32 @func(i32 %x) {
  ret i32 %x
}
; FUNC: define i32 @func
; FUNC-NOT: define i32 @func

define i32 @caller(i32 %y) {
  %z = call i32 @func(i32 %y)
  ret i32 %z
}
; CALLER: define i32 @caller
; CALLER-NOT: define i32 @caller

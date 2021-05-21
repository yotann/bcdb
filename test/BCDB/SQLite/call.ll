; RUN: rm -rf %t
; RUN: bcdb init -uri sqlite:%t
; RUN: llvm-as < %s | bcdb add -uri sqlite:%t -name a -

; RUN: not bcdb evaluate -uri sqlite:%t myfunc 1

; RUN: bcdb cache -uri sqlite:%t -result 1 myfunc 1
; RUN: bcdb evaluate -uri sqlite:%t myfunc 1 | FileCheck %s
; CHECK: 1

; RUN: memodb refs-to -uri sqlite:%t id:1 | FileCheck --check-prefix=REFS %s
; REFS: call:myfunc/1

; RUN: memodb list-calls -uri sqlite:%t myfunc | FileCheck --check-prefix=CALLS %s
; CALLS: call:myfunc/1

; RUN: bcdb invalidate -uri sqlite:%t myfunc
; RUN: not bcdb evaluate -uri sqlite:%t myfunc 1

define i32 @func(i32 %x, i32 %y) {
  %z = add i32 %x, %y
  ret i32 %z
}

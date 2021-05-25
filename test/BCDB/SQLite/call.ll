; RUN: rm -rf %t
; RUN: bcdb init -uri sqlite:%t
; RUN: llvm-as < %s | bcdb add -uri sqlite:%t -name a -

; RUN: not bcdb evaluate -uri sqlite:%t myfunc $(bcdb list-function-ids -uri sqlite:%t)

; RUN: bcdb cache -uri sqlite:%t -result $(bcdb list-function-ids -uri sqlite:%t) myfunc $(bcdb list-function-ids -uri sqlite:%t)
; RUN: bcdb evaluate -uri sqlite:%t myfunc $(bcdb list-function-ids -uri sqlite:%t)

; RUN: memodb refs-to -uri sqlite:%t id:$(bcdb list-function-ids -uri sqlite:%t) | FileCheck --check-prefix=REFS %s
; REFS: call:myfunc/{{[-A-Za-z0-9_=]+$}}

; RUN: memodb list-calls -uri sqlite:%t myfunc | FileCheck --check-prefix=CALLS %s
; CALLS: call:myfunc/{{[-A-Za-z0-9_=]+$}}

; RUN: bcdb invalidate -uri sqlite:%t myfunc
; RUN: not bcdb evaluate -uri sqlite:%t myfunc $(bcdb list-function-ids -uri sqlite:%t)

define i32 @func(i32 %x, i32 %y) {
  %z = add i32 %x, %y
  ret i32 %z
}

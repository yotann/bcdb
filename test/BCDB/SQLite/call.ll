; RUN: rm -rf %t
; RUN: memodb init -store sqlite:%t
; RUN: llvm-as < %s | bcdb add -store sqlite:%t -name a -

; RUN: not memodb put -store sqlite:%t /call/myfunc/$(bcdb list-function-ids -store sqlite:%t)

; RUN: memodb set -store sqlite:%t /call/myfunc/$(bcdb list-function-ids -store sqlite:%t) /cid/$(bcdb list-function-ids -store sqlite:%t)
; RUN: memodb put -store sqlite:%t /call/myfunc/$(bcdb list-function-ids -store sqlite:%t)

; RUN: memodb refs-to -store sqlite:%t /cid/$(bcdb list-function-ids -store sqlite:%t) | FileCheck --check-prefix=REFS %s
; REFS: /call/myfunc/{{[-A-Za-z0-9_=]+$}}

; RUN: memodb list-calls -store sqlite:%t myfunc | FileCheck --check-prefix=CALLS %s
; CALLS: /call/myfunc/{{[-A-Za-z0-9_=]+$}}

; RUN: memodb invalidate -store sqlite:%t myfunc
; RUN: not memodb put -store sqlite:%t /call/myfunc/$(bcdb list-function-ids -store sqlite:%t)

define i32 @func(i32 %x, i32 %y) {
  %z = add i32 %x, %y
  ret i32 %z
}

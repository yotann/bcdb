; RUN: memodb init -store sqlite:%t
; RUN: llvm-as < %s | bcdb add -store sqlite:%t -
; RUN: bcdb mux -store sqlite:%t - | lli - -

; RUN: memodb init -store sqlite:%t.rg
; RUN: llvm-as < %s | bcdb add -store sqlite:%t.rg - -rename-globals
; RUN: bcdb mux -store sqlite:%t.rg - | lli - -

@global = internal global void()* @func

declare void @exit(i32)

define internal void @func() {
  call void @exit(i32 0)
  unreachable
}

define i32 @main(i32, i8**) {
  %f = load void()*, void()** @global
  call void %f()
  ret i32 1
}

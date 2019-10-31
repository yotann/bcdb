; RUN: bcdb init -uri sqlite:%t
; RUN: llvm-as < %s | bcdb add -uri sqlite:%t -
; RUN: bcdb mux -uri sqlite:%t - | lli - -

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

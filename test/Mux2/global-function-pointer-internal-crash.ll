; XFAIL: *

; RUN: bcdb add -uri sqlite:%t.bcdb %s -name x
; RUN: bcdb add -uri sqlite:%t.bcdb %s -name y
; RUN: bcdb mux2 -uri sqlite:%t.bcdb x y -o %t --muxed-name=libmuxed.so

@global = global void ()* @func
@global2 = internal global void ()* @func

declare void @exit(i32)

define internal void @func() {
  call void @exit(i32 0)
  unreachable
}

define i32 @main(i32, i8**) {
  %f = load void()*, void()** @global
  call void %f()
  %f2 = load void()*, void()** @global2
  call void %f2()
  ret i32 1
}

; MUXED: @global = external global void ()*
; MUXED: @global2 = internal global void ()* @func
; MUXED: define internal void @func()
; MUXED-NEXT: call void @__bcdb_id_{{.*}}()

; STUB: @global = global void ()* @func
; STUB-NOT: @global2
; STUB: define internal void @func()
; STUB-NEXT: call void @__bcdb_id_{{.*}}()

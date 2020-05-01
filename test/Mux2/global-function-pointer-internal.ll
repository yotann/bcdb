; RUN: bcdb init -uri sqlite:%t.bcdb
; RUN: bcdb add -uri sqlite:%t.bcdb %s -name prog
; RUN: bcdb mux2 -uri sqlite:%t.bcdb prog -o %t --muxed-name=libmuxed.so --weak-name=libweak.so
; RUN: opt -verify -S < %t/libmuxed.so | FileCheck --check-prefix=MUXED %s
; RUN: opt -verify -S < %t/prog        | FileCheck --check-prefix=STUB  %s
; RUN: opt -verify -S < %t/libweak.so  | FileCheck --check-prefix=WEAK  %s

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

; WEAK: @global = weak global void ()* null

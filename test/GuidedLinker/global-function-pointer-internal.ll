; RUN: bcdb init -store sqlite:%t.bcdb
; RUN: bcdb add -store sqlite:%t.bcdb %s -name prog
; RUN: bcdb gl -store sqlite:%t.bcdb prog -o %t --merged-name=libmerged.so --weak-name=libweak.so --noplugin
; RUN: opt -verify -S < %t/libmerged.so | FileCheck --check-prefix=MERGED %s
; RUN: opt -verify -S < %t/prog        | FileCheck --check-prefix=STUB  %s

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

; MERGED: @global = extern_weak global void ()*
; MERGED: @global2 = internal global void ()* @__bcdb_merged_func
; MERGED: define protected void @__bcdb_merged_func()
; MERGED-NEXT: call void @__bcdb_body_func()

; STUB-NOT: @global2
; STUB-NOT: @__bcdb_merged_global2
; STUB-NOT: @__bcdb_merged_global
; STUB: @global = global void ()* @__bcdb_merged_func
; STUB: declare void @__bcdb_merged_func()

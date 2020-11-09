; REQUIRES: llvm10

; RUN: bcdb init -uri sqlite:%t.bcdb
; RUN: bcdb add -uri sqlite:%t.bcdb %s -name prog
; RUN: bcdb gl -uri sqlite:%t.bcdb prog -o %t --muxed-name=libmuxed.so --noplugin
; RUN: opt -verify -S < %t/libmuxed.so | FileCheck --check-prefix=MUXED %s
; RUN: opt -verify -S < %t/prog        | FileCheck --check-prefix=PROG  %s

define void @func() unnamed_addr {
  call void @llvm.assume(i1 true)
  call void @func()
  ret void
}

; Function Attrs: nounwind willreturn
declare void @llvm.assume(i1) #0

attributes #0 = { nounwind }

; MUXED: @func = internal alias void (), void ()* @__bcdb_body_func
; MUXED: define protected void @__bcdb_body_func()
; MUXED-NEXT: call void @func()

; PROG: define void @func()
; PROG-NEXT: call void @__bcdb_body_func()

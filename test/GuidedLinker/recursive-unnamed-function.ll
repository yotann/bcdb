; REQUIRES: llvm10

; RUN: memodb init -store sqlite:%t.bcdb
; RUN: bcdb add -store sqlite:%t.bcdb %s -name prog
; RUN: bcdb gl -store sqlite:%t.bcdb prog -o %t --merged-name=libmerged.so --noplugin
; RUN: opt -verify -S < %t/libmerged.so | FileCheck --check-prefix=MERGED %s
; RUN: opt -verify -S < %t/prog        | FileCheck --check-prefix=PROG  %s

define void @func() unnamed_addr {
  call void @llvm.assume(i1 true)
  call void @func()
  ret void
}

; Function Attrs: nounwind willreturn
declare void @llvm.assume(i1) #0

attributes #0 = { nounwind }

; MERGED: @func = internal alias void (), void ()* @__bcdb_body_func
; MERGED: define protected void @__bcdb_body_func()
; MERGED: call void @func()

; PROG: define void @func()
; PROG-NEXT: call void @__bcdb_body_func()

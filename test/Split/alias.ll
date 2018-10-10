; RUN: llvm-as < %s | bc-split -o %t
; RUN: llvm-dis < %t/functions/f      | FileCheck --check-prefix=DEFINE %s
; RUN: llvm-dis < %t/remainder/module | FileCheck --check-prefix=MODULE %s

; DEFINE-NOT: @v
@v = global i32 0

; DEFINE: @w = external global i32
@w = alias i32, i32* @v

define void @f() {
  call void (...) @g()
  call void @h()
  load i32, i32* @w
  ret void
}

; DEFINE: declare void @g(...)
; MODULE: @g = alias void (...), bitcast (void ()* @f to void (...)*)
@g = alias void (...), bitcast (void ()* @f to void (...)*)

; DEFINE: declare void @h()
; MODULE: @h = weak hidden alias void (), void ()* @f
@h = weak hidden alias void (), void ()* @f

; RUN: llvm-as < %s | bc-split - %t
; RUN: llvm-dis < %t/functions/f | FileCheck --check-prefix=F %s
; RUN: llvm-dis < %t/functions/g | FileCheck --check-prefix=G %s
; RUN: llvm-dis < %t/remainder/module | FileCheck --check-prefix=MODULE %s

; F: define void @0() {
; G-NOT: @f
; MODULE: declare void @f()
define void @f() {
  call void @g()
  ret void
}

; F: declare void @g()
; G: define void @0() personality i8* bitcast (i32 (...)* @p to i8*)
; MODULE: declare void @g()
define void @g() personality i8* bitcast (i32 (...)* @p to i8*) {
  ret void
}

; F-NOT: declare i32 @p(...)
; G: declare i32 @p(...)
; MODULE: declare i32 @p(...)
declare i32 @p(...)

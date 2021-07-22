; RUN: llvm-as < %s | bc-split -o %t
; RUN: llvm-dis < %t/functions/f.bc      | FileCheck --check-prefix=F %s
; RUN: llvm-dis < %t/functions/g.bc      | FileCheck --check-prefix=G %s
; RUN: llvm-dis < %t/remainder/module.bc | FileCheck --check-prefix=MODULE %s
; RUN: bc-join %t | llvm-dis             | FileCheck --check-prefix=JOINED %s

; MODULE: define void @f()
; MODULE-NEXT: unreachable
; F: define void @0() {
; G-NOT: @f
define void @f() {
  call void @g()
  ret void
}

; MODULE: define void @g() {
; MODULE-NEXT: unreachable
; F: declare void @g()
; G: define void @0() personality i8* bitcast (i32 (...)* @p to i8*)
; JOINED: define void @g() personality i8* bitcast (i32 (...)* @p to i8*)
define void @g() personality i8* bitcast (i32 (...)* @p to i8*) {
  ret void
}

; MODULE: declare i32 @p(...)
; F-NOT: declare i32 @p(...)
; G: declare i32 @p(...)
declare i32 @p(...)

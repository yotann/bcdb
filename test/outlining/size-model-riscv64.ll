; REQUIRES: llvm12

; RUN: %outopt -size-model -analyze %s | FileCheck %s

target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n64-S128"
target triple = "riscv64-unknown-linux-gnu"

; CHECK: estimated call instruction size: 8 bytes
; CHECK: estimated function size without callees: 4 bytes
; CHECK: estimated function size with callees: 36 bytes

; CHECK: define i32 @collatz_len(i32 %0)
define i32 @collatz_len(i32 %0) {
; CHECK-NEXT: %2 = {{.*}} ; {{[0-9]+}} bytes
  %2 = icmp slt i32 %0, 1
; CHECK-NEXT: br i1 %2, {{.*}} ; 4 bytes
  br i1 %2, label %5, label %3

; CHECK: 3:
3:
; CHECK-NEXT: %4 = {{.*}} ; 0 bytes
  %4 = icmp eq i32 %0, 1
; CHECK-NEXT: br i1 %4, {{.*}} ; 4 bytes
  br i1 %4, label %17, label %6

; CHECK: 5:
5:
; CHECK-NEXT: tail call void @abort() ; 8 bytes
  tail call void @abort()
; CHECK-NEXT: unreachable ; 0 bytes
  unreachable

; CHECK: 6:
6:
; CHECK-NEXT: %7 = {{.*}} ; {{[0-9]+}} bytes
  %7 = phi i32 [ %15, %6 ], [ 0, %3 ]
; CHECK-NEXT: %8 = {{.*}} ; 0 bytes
  %8 = phi i32 [ %14, %6 ], [ %0, %3 ]
; CHECK-NEXT: %9 = {{.*}} ; 4 bytes
  %9 = and i32 %8, 1
; CHECK-NEXT: %10 = {{.*}} ; 0 bytes
  %10 = icmp eq i32 %9, 0
; CHECK-NEXT: %11 = {{.*}} ; {{[0-9]+}} bytes
  %11 = mul i32 %8, 3
; CHECK-NEXT: %12 = {{.*}} ; 0 bytes
  %12 = add i32 %11, 1
; CHECK-NEXT: %13 = {{.*}} ; 0 bytes
  %13 = lshr i32 %8, 1
; CHECK-NEXT: %14 = {{.*}} ; 8 bytes
  %14 = select i1 %10, i32 %13, i32 %12
; CHECK-NEXT: %15 = {{.*}} ; 4 bytes
  %15 = add i32 %7, 1
; CHECK-NEXT: %16 = {{.*}} ; 0 bytes
  %16 = icmp sgt i32 %14, 1
; CHECK-NEXT: br i1 %16, {{.*}} ; 4 bytes
  br i1 %16, label %6, label %17

; CHECK: 17:
17:
; CHECK-NEXT: %18 = {{.*}} ; {{[0-9]+}} bytes
  %18 = phi i32 [ 0, %3 ], [ %15, %6 ]
; CHECK-NEXT: ret i32 %18 ; {{[0-9]+}} bytes
  ret i32 %18
}

declare void @abort()

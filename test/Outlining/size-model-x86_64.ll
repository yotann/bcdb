; RUN: opt -load %shlibdir/BCDBOutliningPlugin%shlibext \
; RUN:     -size-model -analyze %s | FileCheck %s

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; CHECK: define i32 @collatz_len(i32 %0)
define i32 @collatz_len(i32 %0) {
; CHECK-NEXT: ; {{[0-9]+}} bytes
; CHECK-NEXT: %2 =
  %2 = icmp slt i32 %0, 1
; CHECK-NEXT: ; 2 bytes
; CHECK-NEXT: br i1 %2
  br i1 %2, label %5, label %3

; CHECK: 3:
3:
; CHECK-NEXT: ; 3 bytes
; CHECK-NEXT: %4 =
  %4 = icmp eq i32 %0, 1
; CHECK-NEXT: ; 2 bytes
; CHECK-NEXT: br i1 %4
  br i1 %4, label %17, label %6

; CHECK: 5:
5:
; CHECK-NEXT: ; 5 bytes
; CHECK-NEXT: tail call void @abort()
  tail call void @abort()
; CHECK-NEXT: ; 0 bytes
; CHECK-NEXT: unreachable
  unreachable

; CHECK: 6:
6:
; CHECK-NEXT: ; 0 bytes
; CHECK-NEXT: %7 =
  %7 = phi i32 [ %15, %6 ], [ 0, %3 ]
; CHECK-NEXT: ; 0 bytes
; CHECK-NEXT: %8 =
  %8 = phi i32 [ %14, %6 ], [ %0, %3 ]
; CHECK-NEXT: ; 0 bytes
; CHECK-NEXT: %9 =
  %9 = and i32 %8, 1
; CHECK-NEXT: ; 4 bytes
; CHECK-NEXT: %10 =
  %10 = icmp eq i32 %9, 0
; CHECK-NEXT: ; 0 bytes
; CHECK-NEXT: %11 =
  %11 = mul i32 %8, 3
; CHECK-NEXT: ; 4 bytes
; CHECK-NEXT: %12 =
  %12 = add i32 %11, 1
; CHECK-NEXT: ; 4 bytes
; CHECK-NEXT: %13 =
  %13 = lshr i32 %8, 1
; CHECK-NEXT: ; 3 bytes
; CHECK-NEXT: %14 =
  %14 = select i1 %10, i32 %13, i32 %12
; CHECK-NEXT: ; 2 bytes
; CHECK-NEXT: %15 =
  %15 = add i32 %7, 1
; CHECK-NEXT: ; 3 bytes
; CHECK-NEXT: %16 =
  %16 = icmp sgt i32 %14, 1
; CHECK-NEXT: ; 2 bytes
; CHECK-NEXT: br i1 %16
  br i1 %16, label %6, label %17

; CHECK: 17:
17:
; CHECK-NEXT: ; 0 bytes
; CHECK-NEXT: %18 =
  %18 = phi i32 [ 0, %3 ], [ %15, %6 ]
; CHECK-NEXT: ; 2 bytes
; CHECK-NEXT: ret i32 %18
  ret i32 %18
}

declare void @abort()

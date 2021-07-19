; RUN: opt -load %shlibdir/BCDBOutliningPlugin%shlibext \
; RUN:     -size-model -disable-output %s

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

define void @f() {
  %x = add i32 1, 2
  ret void

dead0:
  ret void

dead1:
  br i1 undef, label %dead0, label %dead1
}

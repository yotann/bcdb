; RUN: opt -load %shlibdir/BCDBOutliningPlugin%shlibext \
; RUN:     -size-model -disable-output %s

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; CloneModule may replace the phi with a constant, and SizeModel needs to
; handle it correctly.

define i32 @f(i1 %cond) {
  br i1 %cond, label %if0, label %if1

if0:
  br label %end

if1:
  br label %end

end:
  %x = phi i32 [ undef, %if0 ], [ undef, %if1 ]
  ret i32 %x
}

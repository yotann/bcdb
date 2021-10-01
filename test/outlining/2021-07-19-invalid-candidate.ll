; RUN: %outopt -outlining-candidates -analyze %s

source_filename = "crashing.ll"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; OutliningCandidates generated invalid candidate: 4-10, 12-17

define void @balance() {
bb:
  br label %bb161

bb161:
  br i1 undef, label %bb162, label %bb165

bb162:
  br i1 undef, label %bb163, label %bb164

bb163:
  call void @computeCellSize()
  br label %bb164

bb164:
  br label %bb165

bb165:
  %i = phi i32 [ undef, %bb164 ], [ 0, %bb161 ]
  %i166 = load i32, i32* undef, align 4
  %i167 = sub i32 %i166, %i
  br i1 undef, label %bb161, label %bb373

bb373:
  ret void
}

declare void @computeCellSize()

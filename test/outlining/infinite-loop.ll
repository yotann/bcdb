; RUN: opt -load %shlibdir/BCDBOutliningPlugin%shlibext \
; RUN:     -outlining-extractor -outline-unprofitable -verify -S %s

@global = external global i32

define void @f() {
start:
  br label %loop0

loop0:
  %tmp0 = load volatile i32, i32* @global
  br label %loop1

loop1:
  %tmp = phi i32 [ %tmp0, %loop0 ], [ %tmp1, %loop1 ]
  store volatile i32 %tmp, i32* @global
  %tmp1 = load volatile i32, i32* @global
  br i1 undef, label %loop0, label %loop1
}

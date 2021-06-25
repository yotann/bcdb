; RUN: rm -rf %t
; RUN: bcdb init -store rocksdb:%t
; RUN: llvm-as < %s | bcdb add -store rocksdb:%t -
; RUN: bcdb get -store rocksdb:%t -name - | opt -verify -S | FileCheck %s

; CHECK: @.sh.2909090869 = private constant [4 x i8] c"foo\00"
@.str.1.2 = private constant [4 x i8] c"foo\00"

; CHECK: @.str.3.4 = constant [4 x i8] c"bar\00"
@.str.3.4 = constant [4 x i8] c"bar\00"

; CHECK: @.sh.2738797813 = private constant [4 x i8] zeroinitializer
@.str = private constant [4 x i8] zeroinitializer

define i8* @func() {
  ; CHECK: getelementptr inbounds ([4 x i8], [4 x i8]* @.sh.2909090869, i64 0, i64 0)
  ret i8* getelementptr inbounds ([4 x i8], [4 x i8]* @.str.1.2, i64 0, i64 0)
}

define i8* @func2() {
  ; CHECK: getelementptr inbounds ([4 x i8], [4 x i8]* @.str.3.4, i64 0, i64 0)
  ret i8* getelementptr inbounds ([4 x i8], [4 x i8]* @.str.3.4, i64 0, i64 0)
}

define i8* @func3() {
  ; CHECK: getelementptr inbounds ([4 x i8], [4 x i8]* @.sh.2738797813, i64 0, i64 0)
  ret i8* getelementptr inbounds ([4 x i8], [4 x i8]* @.str, i64 0, i64 0)
}

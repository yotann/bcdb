; RUN: bcdb init -uri sqlite:%t
; RUN: llvm-as < %s | bcdb add -uri sqlite:%t -
; RUN: bcdb get -uri sqlite:%t -name - | opt -verify -S | FileCheck %s

; CHECK: @.sh.13365 = private constant [4 x i8] c"foo\00"
@.str.1.2 = private constant [4 x i8] c"foo\00"

; CHECK: @.str.3.4 = constant [4 x i8] c"bar\00"
@.str.3.4 = constant [4 x i8] c"bar\00"

; CHECK: @.sh.48373 = private constant [4 x i8] zeroinitializer
@.str.5.6 = private constant [4 x i8] zeroinitializer

define i8* @func() {
  ; CHECK: getelementptr inbounds ([4 x i8], [4 x i8]* @.sh.13365, i64 0, i64 0)
  ret i8* getelementptr inbounds ([4 x i8], [4 x i8]* @.str.1.2, i64 0, i64 0)
}

define i8* @func2() {
  ; CHECK: getelementptr inbounds ([4 x i8], [4 x i8]* @.str.3.4, i64 0, i64 0)
  ret i8* getelementptr inbounds ([4 x i8], [4 x i8]* @.str.3.4, i64 0, i64 0)
}

define i8* @func3() {
  ; CHECK: getelementptr inbounds ([4 x i8], [4 x i8]* @.sh.48373, i64 0, i64 0)
  ret i8* getelementptr inbounds ([4 x i8], [4 x i8]* @.str.5.6, i64 0, i64 0)
}

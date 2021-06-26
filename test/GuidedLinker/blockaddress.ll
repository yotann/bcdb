; RUN: memodb init -store sqlite:%t.bcdb
; RUN: bcdb add -store sqlite:%t.bcdb %s -name prog

; RUN: bcdb gl -store sqlite:%t.bcdb prog -o %t-noweak --merged-name=libmerged.so --noweak

; RUN: bcdb gl -store sqlite:%t.bcdb prog -o %t-unconstrained --merged-name=libmerged.so
; RUN: opt -verify -S < %t-unconstrained/prog | FileCheck --check-prefix=OPEN-PROG %s

@blockaddresses = internal constant [2 x i8*] [i8* blockaddress(@g, %bb2), i8* blockaddress(@h, %1)]

define i8* @f() {
  ret i8* blockaddress(@g, %1)
}

define i8* @g() {
  ret i8* blockaddress(@g, %1)
; <label>:1:
  ret i8* null
bb2:
  ret i8* null
}

define [2 x i8*]* @h() {
  ret [2 x i8*]* @blockaddresses
; <label>:1:
  unreachable
}

; OPEN-PROG: @blockaddresses = internal constant [2 x i8*] [i8* blockaddress(@g, %bb2), i8* blockaddress(@h, %1)]

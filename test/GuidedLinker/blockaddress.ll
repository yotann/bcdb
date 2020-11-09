; RUN: bcdb init -uri sqlite:%t.bcdb
; RUN: bcdb add -uri sqlite:%t.bcdb %s -name prog

; RUN: bcdb gl -uri sqlite:%t.bcdb prog -o %t-spurious --muxed-name=libmuxed.so --noweak

; RUN: bcdb gl -uri sqlite:%t.bcdb prog -o %t-open --muxed-name=libmuxed.so
; RUN: opt -verify -S < %t-open/prog | FileCheck --check-prefix=OPEN-PROG %s

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

; RUN: llvm-as < %s | bc-split -o %t
; RUN: llvm-dis < %t/remainder/module   | FileCheck --check-prefix=MODULE %s
; RUN: bc-join %t | llvm-dis            | FileCheck --check-prefix=MODULE %s

; MODULE: @blockaddresses = constant [2 x i8*] [i8* blockaddress(@g, %1), i8* blockaddress(@h, %1)]
@blockaddresses = constant [2 x i8*] [i8* blockaddress(@g, %1), i8* blockaddress(@h, %1)]

; MODULE: define i8* @f()
define i8* @f() {
  ; MODULE: ret i8* blockaddress(@g, %bb2)
  ret i8* blockaddress(@g, %bb2)
}

; MODULE: define i8* @g()
define i8* @g() {
  ; MODULE: ret i8* blockaddress(@g, %1)
  ret i8* blockaddress(@g, %1)
; <label>:1:
  ret i8* null
bb2:
  ret i8* null
}

; MODULE: define void @h()
define void @h() {
  unreachable
; <label>:1:
  unreachable
}

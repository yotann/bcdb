; RUN: llvm-as < %s | bc-split -o %t
; RUN: bc-join %t | llvm-dis          | FileCheck --check-prefix=JOINED %s

@x = private global i32 0

; JOINED: define i32* @f()
define i32* @f() {
  ; JOINED-NEXT: call void @h()
  call void @h()
  ; JOINED-NEXT: ret i32* @x
  ret i32* @x
}

; JOINED: define i32* @g()
define i32* @g() {
  ; JOINED-NEXT: call void @h()
  call void @h()
  ; JOINED-NEXT: ret i32* @x
  ret i32* @x
}

define internal void @h() {
  ret void
}

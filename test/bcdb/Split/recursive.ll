; RUN: llvm-as < %s | bc-split -o %t
; RUN: llvm-dis < %t/functions/f.bc | FileCheck --check-prefix=DEFINE %s
; RUN: bc-join %t | llvm-dis        | FileCheck --check-prefix=JOINED %s

; DEFINE: define void @0()
; JOINED: define void @f()
define void @f() {
  ; DEFINE-NEXT: call void @0()
  ; JOINED-NEXT: call void @f()
  call void @f()
  ret void
}

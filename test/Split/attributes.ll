; RUN: llvm-as < %s | bc-split -o %t
; RUN: llvm-dis < %t/functions/f      | FileCheck --check-prefix=DEFINE %s
; RUN: llvm-dis < %t/remainder/module | FileCheck --check-prefix=MODULE %s
; RUN: bc-join %t | llvm-dis          | FileCheck --check-prefix=JOINED %s

; JOINED: declare void @g() #0
; JOINED: define void @f(i8 inreg) #1
; JOINED: attributes #0 = { alignstack=8 }
; JOINED: attributes #1 = { alignstack=4 }

; MODULE: define void @f(i8 inreg) #0
; MODULE-NEXT: unreachable
; DEFINE: define void @0(i8 inreg) #0
define void @f(i8 inreg) #0 {
  ret void
}

; MODULE: declare void @g() #1
declare void @g() #1

; MODULE: attributes #0 = { alignstack=4 }
; DEFINE: attributes #0 = { alignstack=4 }
attributes #0 = { alignstack=4 }

; MODULE: attributes #1 = { alignstack=8 }
; DEFINE-NOT: attributes #1
attributes #1 = { alignstack=8 }

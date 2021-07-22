; RUN: llvm-as < %s | bc-split -o %t
; RUN: llvm-dis < %t/functions/f.bc      | FileCheck --check-prefix=DEFINE %s
; RUN: llvm-dis < %t/remainder/module.bc | FileCheck --check-prefix=MODULE %s
; RUN: bc-join %t | llvm-dis             | FileCheck --check-prefix=JOINED %s

; MODULE: define void @f(i8 inreg %arg) #0
; MODULE-NEXT: unreachable
; DEFINE: define void @0(i8 inreg %arg) #0
; JOINED: define void @f(i8 inreg %arg) #0
define void @f(i8 inreg %arg) #0 {
  ret void
}

; MODULE: declare void @g() #1
; JOINED: declare void @g() #1
declare void @g() #1

; MODULE: attributes #0 = { alignstack=4 }
; DEFINE: attributes #0 = { alignstack=4 }
; JOINED: attributes #0 = { alignstack=4 }
attributes #0 = { alignstack=4 }

; MODULE: attributes #1 = { alignstack=8 }
; DEFINE-NOT: attributes #1
; JOINED: attributes #1 = { alignstack=8 }
attributes #1 = { alignstack=8 }

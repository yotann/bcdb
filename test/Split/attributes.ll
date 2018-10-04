; RUN: llvm-as < %s | bc-split - %t
; RUN: llvm-dis < %t/functions/f | FileCheck --check-prefix=DEFINE %s
; RUN: llvm-dis < %t/remainder/module | FileCheck --check-prefix=MODULE %s

; MODULE: define void @f() #0
; MODULE-NEXT: unreachable
; DEFINE: define void @0() #0
define void @f() #0 {
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

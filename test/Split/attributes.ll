; RUN: llvm-as < %s | bc-split - %t
; RUN: llvm-dis < %t/functions/f | FileCheck --check-prefix=F %s
; RUN: llvm-dis < %t/remainder/module | FileCheck --check-prefix=MODULE %s

; F: define void @0() #0
; MODULE: declare void @f() #0
define void @f() #0 {
  ret void
}

; MODULE: declare void @g() #1
declare void @g() #1

; F: attributes #0 = { alignstack=4 }
; MODULE: attributes #0 = { alignstack=4 }
attributes #0 = { alignstack=4 }

; F-NOT: attributes #1
; MODULE: attributes #1 = { alignstack=8 }
attributes #1 = { alignstack=8 }

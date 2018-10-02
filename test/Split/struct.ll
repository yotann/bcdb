; RUN: llvm-as < %s | bc-split - %t
; RUN: llvm-dis < %t/functions/f | FileCheck --check-prefix=F %s
; RUN: llvm-dis < %t/functions/g | FileCheck --check-prefix=G %s
; RUN: llvm-dis < %t/remainder/module | FileCheck --check-prefix=MODULE %s

; F: %mytype = type { %mytype*, i32 }
; G-NOT: %mytype
; MODULE: %mytype = type { %mytype*, i32 }
%mytype = type { %mytype*, i32 }

define void @f(%mytype) {
  ret void
}

define void @g() {
  ret void
}

; REQUIRES: llvm12
; RUN: llvm-as < %s | bc-split -o %t
; RUN: llvm-dis < %t/functions/f.bc      | FileCheck --check-prefix=F %s
; RUN: llvm-dis < %t/remainder/module.bc | FileCheck --check-prefix=MODULE %s
; RUN: bc-join %t | llvm-dis             | FileCheck --check-prefix=JOINED %s

; F: %0 = type { i32 }
; MODULE: %mytype = type { i32 }
; JOINED: %mytype = type { i32 }
%mytype = type { i32 }

; F: define void @0(%0* noalias sret(%0) %0, i32 %1)
; MODULE: define void @f(%mytype* noalias sret(%mytype) %0, i32 %1)
; JOINED: define void @f(%mytype* noalias sret(%mytype) %0, i32 %1)
define void @f(%mytype* noalias sret(%mytype) %0, i32 %1) {
  ret void
}

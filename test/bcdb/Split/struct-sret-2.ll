; REQUIRES: llvm12
; RUN: llvm-as < %s | bc-split -o %t
; RUN: llvm-dis < %t/functions/caller.bc | FileCheck --check-prefix=CALLER %s
; RUN: llvm-dis < %t/remainder/module.bc | FileCheck --check-prefix=MODULE %s
; RUN: bc-join %t | llvm-dis             | FileCheck --check-prefix=JOINED %s

; CALLER: %0 = type { i8 }
; MODULE: %struct = type { i8 }
; JOINED: %struct = type { i8 }
%struct = type { i8 }

; CALLER: call void undef(%0* sret(%0) %0)
; JOINED: call void undef(%struct* sret(%struct) %0)

define void @caller(%struct* %0) {
  call void undef(%struct* sret(%struct) %0)
  ret void
}

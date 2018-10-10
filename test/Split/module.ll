; RUN: llvm-as < %s | bc-split -o %t
; RUN: llvm-dis < %t/functions/f      | FileCheck --check-prefix=DEFINE %s
; RUN: llvm-dis < %t/remainder/module | FileCheck --check-prefix=MODULE %s
; RUN: bc-join %t | llvm-dis          | FileCheck --check-prefix=JOINED %s

; MODULE: source_filename = "source-filename.c"
; DEFINE-NOT: source_filename
; JOINED: source_filename = "source-filename.c"
source_filename = "source-filename.c"

; MODULE: target datalayout = "E"
; DEFINE: target datalayout = "E"
; JOINED: target datalayout = "E"
target datalayout = "E"

; MODULE: target triple = "x86_64-apple-macosx10.10.0"
; DEFINE: target triple = "x86_64-apple-macosx10.10.0"
; JOINED: target triple = "x86_64-apple-macosx10.10.0"
target triple = "x86_64-apple-macosx10.10.0"

; MODULE: module asm "beep boop"
; DEFINE-NOT: module asm
; JOINED: module asm "beep boop"
module asm "beep boop"

; DEFINE: define void @0()
define void @f() {
  ret void
}

; MODULE: !llvm.module.flags = !{!0}
; MODULE: !0 = !{i32 1, !"mod1", i32 0}
; DEFINE-NOT: !llvm.module.flags
; DEFINE-NOT: !0 =
; JOINED: !llvm.module.flags = !{!0}
; JOINED: !0 = !{i32 1, !"mod1", i32 0}
!llvm.module.flags = !{!0}
!0 = !{i32 1, !"mod1", i32 0}

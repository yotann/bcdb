; REQUIRES: llvm9
; RUN: llvm-as < %s | bc-split -o %t
; RUN: llvm-dis < %t/functions/npy_cargl.bc | FileCheck %s
; RUN: bc-join %t   | opt -verify -S        | FileCheck --check-prefix=JOINED %s

; CHECK: define x86_fp80 @0()
; JOINED: define x86_fp80 @npy_cargl()
define x86_fp80 @npy_cargl() local_unnamed_addr {
  ; CHECK-NEXT: call x86_fp80 @cargl({ x86_fp80, x86_fp80 }* nonnull byval({ x86_fp80, x86_fp80 }) align 16 undef)
  ; JOINED-NEXT: call x86_fp80 @cargl({ x86_fp80, x86_fp80 }* nonnull byval({ x86_fp80, x86_fp80 }) align 16 undef)
  %x = tail call x86_fp80 @cargl({ x86_fp80, x86_fp80 }* nonnull byval({ x86_fp80, x86_fp80 }) align 16 undef)
  ret x86_fp80 %x
}

declare x86_fp80 @cargl({ x86_fp80, x86_fp80 }* byval({ x86_fp80, x86_fp80 }) align 16) local_unnamed_addr

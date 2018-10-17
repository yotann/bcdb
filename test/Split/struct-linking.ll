; RUN: llvm-as < %s | bc-split -o %t
; RUN: bc-join %t | llvm-dis          | FileCheck --check-prefix=JOINED %s
; Issue #8
; XFAIL: *

%s = type { i16 }
%s.0 = type opaque

declare void @f(%s)

define void @g(i8*) {
  ; JOINED: bitcast i8* %0 to %s.0*
  bitcast i8* %0 to %s.0*
  ret void
}

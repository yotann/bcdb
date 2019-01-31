; RUN: llvm-as < %s | bc-split -o %t
; RUN: llvm-dis < %t/functions/f.bc | FileCheck --check-prefix=DEFINE %s

; DEFINE: %0 = type { i16 }
; DEFINE: %1 = type { i32 }
%s = type { i16 }
%t = type { i32 }

define void @f() {
  call void @g(%s* null, %t* null)
  ret void
}

declare void @g(%s* byval, %t* inalloca)

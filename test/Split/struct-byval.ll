; RUN: llvm-as < %s | bc-split - %t
; RUN: llvm-dis < %t/functions/f | FileCheck --check-prefix=DEFINE %s

; DEFINE: %s = type { i16 }
; DEFINE: %t = type { i32 }
%s = type { i16 }
%t = type { i32 }

define void @f() {
  call void @g(%s* null, %t* null)
  ret void
}

declare void @g(%s* byval, %t* inalloca)

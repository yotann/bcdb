; RUN: llvm-as < %s | bc-split -o %t
; RUN: llvm-dis < %t/functions/f | FileCheck --check-prefix=DEFINE %s

; DEFINE: %s0 = type { i32 }
; DEFINE: %s1 = type { i16 }
%s0 = type { i32 }
%s1 = type { i16 }

define void @f() {
  call void @llvm.codeview.annotation(metadata !0)
  ret void, !foo !1
}

declare void @llvm.codeview.annotation(metadata)

!0 = !{i32 0, %s0 zeroinitializer}
!1 = !{%s1 zeroinitializer}

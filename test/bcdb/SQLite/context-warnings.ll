; RUN: rm -rf %t
; RUN: memodb init -store sqlite:%t
; RUN: llvm-as < %s | bcdb add -store sqlite:%t - 2>&1 | FileCheck %s

; REQUIRES: llvm12

define void @func() {
  %two = add i32 1, 1, !mytag !0
  call void @func() [ "mybundle"(i64 0) ]
  fence syncscope("myscope") acquire
  ret void
}

!0 = !{i32 2}

; CHECK: WARNING: unknown MD kind "mytag", may prevent deduplication
; CHECK: WARNING: unknown operand bundle "mybundle", may prevent deduplication
; CHECK: WARNING: unknown sync scope "myscope", may prevent deduplication

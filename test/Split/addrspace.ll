; REQUIRES: llvm8
; RUN: llvm-as < %s | bc-split -o %t
; RUN: llvm-dis < %t/functions/f.bc      | FileCheck --check-prefix=DEFINE %s
; RUN: llvm-dis < %t/remainder/module.bc | FileCheck --check-prefix=MODULE %s

; NOTE: bc-join doesn't work because of a bug in LLVM.
; https://bugs.llvm.org/show_bug.cgi?id=41154



; MODULE: define void () addrspace(40)* @f() addrspace(1)
; MODULE-NEXT: unreachable
; MODULE: declare void @h() addrspace(40)

; DEFINE: define void () addrspace(40)* @0() addrspace(1)
; DEFINE: call addrspace(5) void @g()
; DEFINE: ret void () addrspace(40)* @h
; DEFINE: declare void @g() addrspace(5)

define void () addrspace(40)* @f() addrspace(1) {
  call addrspace(5) void @g()
  ret void () addrspace(40)* @h
}

define void @g() addrspace(5) {
  ret void
}

declare void @h() addrspace(40)

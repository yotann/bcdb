; RUN: memodb init -store sqlite:%t.bcdb
; RUN: bcdb add -store sqlite:%t.bcdb %s -name prog
; RUN: bcdb gl -store sqlite:%t.bcdb prog -o %t --merged-name=libmerged.so
; RUN: opt -verify -S < %t/libmerged.so | FileCheck --check-prefix=MERGED %s
; RUN: opt -verify -S < %t/prog        | FileCheck --check-prefix=STUB  %s

define i32 @main() {
  ret i32 0
}

!llvm.module.flags = !{!0}
!0 = !{i32 7, !"PIE Level", i32 1}

; MERGED-NOT: PIE Level
; MERGED: !{i32 7, !"PIC Level", i32 2}
; MERGED-NOT: PIE Level

; STUB: !{i32 7, !"PIE Level", i32 1}

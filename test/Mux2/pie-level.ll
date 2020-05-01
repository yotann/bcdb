; RUN: bcdb init -uri sqlite:%t.bcdb
; RUN: bcdb add -uri sqlite:%t.bcdb %s -name prog
; RUN: bcdb mux2 -uri sqlite:%t.bcdb prog -o %t --muxed-name=libmuxed.so
; RUN: opt -verify -S < %t/libmuxed.so | FileCheck --check-prefix=MUXED %s
; RUN: opt -verify -S < %t/prog        | FileCheck --check-prefix=STUB  %s

define i32 @main() {
  ret i32 0
}

!llvm.module.flags = !{!0}
!0 = !{i32 7, !"PIE Level", i32 1}

; MUXED-NOT: PIE Level
; MUXED: !{i32 7, !"PIC Level", i32 2}
; MUXED-NOT: PIE Level

; STUB: !{i32 7, !"PIE Level", i32 1}

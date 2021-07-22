; UNSUPPORTED: llvm12
; RUN: memodb init -store sqlite:%t
; RUN: llvm-as < %s | bcdb add -store sqlite:%t -
; RUN: bcdb merge -store sqlite:%t - | opt -verify -S | FileCheck %s

; see llvm's getParameterABIAttributes for a list of attributes that affect ABI

%struct = type { %struct2* }
%struct2 = type { i32 }

define void @func(%struct* byval) {
  ret void
}
; CHECK: define internal void @__bcdb_body_func(%{{.*}}* byval{{.*}}) {
; CHECK: define void @func(%{{.*}}* byval{{.*}}) #0 {

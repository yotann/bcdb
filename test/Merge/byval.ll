; RUN: bcdb init -uri sqlite:%t
; RUN: llvm-as < %s | bcdb add -uri sqlite:%t -
; RUN: bcdb merge -uri sqlite:%t - | opt -verify -S | FileCheck %s

; see llvm's getParameterABIAttributes for a list of attributes that affect ABI

%struct = type { %struct2* }
%struct2 = type { i32 }

define void @func(%struct* byval) {
  ret void
}
; CHECK: define internal void @__bcdb_id_{{.*}}(%{{.*}}* byval{{.*}}) {
; CHECK: define void @func(%{{.*}}* byval{{.*}}) {

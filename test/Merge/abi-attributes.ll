; RUN: bcdb init -uri sqlite:%t
; RUN: llvm-as < %s | bcdb add -uri sqlite:%t -
; RUN: bcdb merge -uri sqlite:%t - | opt -verify -S | FileCheck %s

; see llvm's getParameterABIAttributes for a list of attributes that affect ABI

define inreg i32 @f.inreg.ret() { unreachable }
; CHECK: define internal inreg i32 @__bcdb_id_{{.*}}() {
; CHECK: define inreg i32 @f.inreg.ret() {

define void @f.inreg(i32 inreg %arg) { unreachable }
; CHECK: define internal void @__bcdb_id_{{.*}}(i32 inreg %arg) {
; CHECK: define void @f.inreg(i32 inreg %arg) {

define void @f.byval(i32* byval %arg) { unreachable }
; CHECK: define internal void @__bcdb_id_{{.*}}(i32* byval{{.*}} %arg) {
; CHECK: define void @f.byval(i32* byval{{.*}} %arg) {

define void @f.align(i32* align 16 %arg) { unreachable }
; CHECK: define internal void @__bcdb_id_{{.*}}(i32* align 16 %arg) {
; CHECK: define void @f.align(i32* align 16 %arg) {

define fastcc void @f.fastcc() { unreachable }
; CHECK: define internal fastcc void @__bcdb_id_{{.*}}() {
; CHECK: define fastcc void @f.fastcc() {

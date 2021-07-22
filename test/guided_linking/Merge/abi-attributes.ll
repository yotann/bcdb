; UNSUPPORTED: llvm12
; RUN: memodb init -store sqlite:%t
; RUN: llvm-as < %s | bcdb add -store sqlite:%t -
; RUN: bcdb merge -store sqlite:%t - | opt -verify -S | FileCheck %s

; see llvm's getParameterABIAttributes for a list of attributes that affect ABI

define inreg i32 @f.inreg.ret() { unreachable }
; CHECK: define internal inreg i32 @__bcdb_body_f.inreg.ret() {

define void @f.inreg(i32 inreg %arg) { unreachable }
; CHECK: define internal void @__bcdb_body_f.inreg(i32 inreg %arg) {

define void @f.byval(i32* byval %arg) { unreachable }
; CHECK: define internal void @__bcdb_body_f.byval(i32* byval{{.*}} %arg) {

define void @f.align(i32* align 16 %arg) { unreachable }
; CHECK: define internal void @__bcdb_body_f.align(i32* align 16 %arg) {

define fastcc void @f.fastcc() { unreachable }
; CHECK: define internal fastcc void @__bcdb_body_f.fastcc() {

; CHECK: define inreg i32 @f.inreg.ret() #0 {
; CHECK: define void @f.inreg(i32 inreg %arg) #0 {
; CHECK: define void @f.byval(i32* byval{{.*}} %arg) #0 {
; CHECK: define void @f.align(i32* align 16 %arg) #0 {
; CHECK: define fastcc void @f.fastcc() #0 {

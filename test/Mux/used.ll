; RUN: bcdb init -uri sqlite:%t
; RUN: llvm-as < %s | bcdb add -uri sqlite:%t - -name a
; RUN: llvm-as < %s | bcdb add -uri sqlite:%t - -name b
; RUN: bcdb mux -uri sqlite:%t a b | opt -verify -S | FileCheck %s

@X = internal global i8 4

; CHECK: @llvm.used = appending global [2 x i8*] [i8* @X{{.*}}, i8* @X{{.*}}], section "llvm.metadata"
; CHECK-NOT: @llvm.used.0
@llvm.used = appending global [1 x i8*] [i8* @X], section "llvm.metadata"

; CHECK: @llvm.compiler.used = appending global [2 x i8*] [i8* @X{{.*}}, i8* @X{{.*}}], section "llvm.metadata"
; CHECK-NOT: @llvm.compiler.used.0
@llvm.compiler.used = appending global [1 x i8*] [i8* @X], section "llvm.metadata"

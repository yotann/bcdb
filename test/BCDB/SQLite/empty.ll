; RUN: rm -rf %t
; RUN: bcdb init -store sqlite:%t
; RUN: llvm-as < %s | bcdb add -store sqlite:%t -
; RUN: bcdb get -store sqlite:%t -name - | opt -verify -S

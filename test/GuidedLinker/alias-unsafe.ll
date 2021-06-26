; REQUIRES: llvm10

; RUN: memodb init -store sqlite:%t.bcdb
; RUN: bcdb add -store sqlite:%t.bcdb %s -name prog
; RUN: bcdb gl -store sqlite:%t.bcdb prog -o %t --merged-name=libmerged.so --weak-name=libweak.so --noweak --nooverride --nouse
; RUN: opt -verify -S < %t/libmerged.so | FileCheck --check-prefix=MERGED %s
; RUN: opt -verify -S < %t/prog        | FileCheck --check-prefix=PROG  %s
; RUN: opt -verify -S < %t/libweak.so  | FileCheck --check-prefix=WEAK  %s

@alias1 = weak_odr alias void (), void ()* @target1
@alias2 = weak_odr alias void (), void ()* @target2

define weak_odr void @target1() {
  call void @alias1()
  ret void
}

define weak_odr void @target2() {
  ret void
}

; MERGED: @alias1 = weak_odr alias void (), void ()* @target1
; MERGED: @alias2 = weak_odr alias void (), void ()* @target2
; MERGED: define internal void @__bcdb_body_target1()
; MERGED-NEXT: call void @alias1()
; MERGED: define internal void @__bcdb_body_target2()
; MERGED-NEXT: ret void
; MERGED: define internal void @target1()
; MERGED-NEXT: call void @__bcdb_body_target1()
; MERGED: define internal void @target2()
; MERGED-NEXT: call void @__bcdb_body_target2()

; PROG-NOT: @alias1
; PROG-NOT: @target1
; PROG-NOT: @alias2
; PROG-NOT: @target2

; WEAK-NOT: @alias1
; WEAK-NOT: @target1
; WEAK-NOT: @alias2
; WEAK-NOT: @target2

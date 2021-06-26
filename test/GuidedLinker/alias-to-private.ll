; XFAIL: *

; RUN: memodb init -store sqlite:%t.bcdb
; RUN: bcdb add -store sqlite:%t.bcdb %s -name prog
; RUN: bcdb gl -store sqlite:%t.bcdb prog -o %t --merged-name=libmerged.so --weak-name=libweak.so
; RUN: opt -verify -S < %t/libmerged.so | FileCheck --check-prefix=MERGED %s
; RUN: opt -verify -S < %t/prog        | FileCheck --check-prefix=STUB  %s
; RUN: opt -verify -S < %t/libweak.so  | FileCheck --check-prefix=WEAK  %s

@weak = weak alias void (), void ()* @internal
@weak_odr = weak_odr alias void (), void ()* @internal
@external = external alias void (), void ()* @internal

define internal void @internal() {
  ret void
}

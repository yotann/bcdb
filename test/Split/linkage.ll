; RUN: llvm-as < %s | bc-split -o %t
; RUN: llvm-dis < %t/functions/f.private | FileCheck --check-prefix=DEFINE %s
; RUN: llvm-dis < %t/remainder/module | FileCheck --check-prefix=MODULE %s
; RUN: bc-join %t | llvm-dis          | FileCheck --check-prefix=JOINED %s

; MODULE: declare extern_weak void @f.extern_weak()
; JOINED: declare extern_weak void @f.extern_weak()
declare extern_weak void @f.extern_weak()

; MODULE: define private void @f.private()
; MODULE-NEXT: unreachable
; DEFINE: define void @0()
; DEFINE: declare void @f.internal()
; DEFINE: declare extern_weak void @f.extern_weak()
; JOINED: define private void @f.private()
define private void @f.private() {
  call void @f.internal()
  call void @f.extern_weak()
  ret void
}

; MODULE: define internal void @f.internal()
; MODULE-NEXT: unreachable
; JOINED: define internal void @f.internal()
define internal void @f.internal() {
  ret void
}

; MODULE: define available_externally void @f.available_externally()
; JOINED: define available_externally void @f.available_externally()
define available_externally void @f.available_externally() {
  ret void
}

; MODULE: define linkonce void @f.linkonce()
; JOINED: define linkonce void @f.linkonce()
define linkonce void @f.linkonce() {
  ret void
}

; MODULE: define weak void @f.weak()
; JOINED: define weak void @f.weak()
define weak void @f.weak() {
  ret void
}

; MODULE: define linkonce_odr void @f.linkonce_odr()
; JOINED: define linkonce_odr void @f.linkonce_odr()
define linkonce_odr void @f.linkonce_odr() {
  ret void
}

; MODULE: define weak_odr void @f.weak_odr()
; JOINED: define weak_odr void @f.weak_odr()
define weak_odr void @f.weak_odr() {
  ret void
}

; MODULE: define void @f.external()
; JOINED: define void @f.external()
define void @f.external() {
  ret void
}

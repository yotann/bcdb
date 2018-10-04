; RUN: llvm-as < %s | bc-split - %t
; RUN: llvm-dis < %t/functions/f.private | FileCheck --check-prefix=PRIVATE %s
; RUN: llvm-dis < %t/remainder/module | FileCheck --check-prefix=MODULE %s

; MODULE: define private void @f.private()
; MODULE-NEXT: unreachable
; PRIVATE: define void @0()
; PRIVATE: declare void @f.internal()
; PRIVATE: declare extern_weak void @f.extern_weak()
define private void @f.private() {
  call void @f.internal()
  call void @f.extern_weak()
  ret void
}

; MODULE: define internal void @f.internal()
; MODULE-NEXT: unreachable
define internal void @f.internal() {
  ret void
}

; MODULE: define available_externally void @f.available_externally()
define available_externally void @f.available_externally() {
  ret void
}

; MODULE: define linkonce void @f.linkonce()
define linkonce void @f.linkonce() {
  ret void
}

; MODULE: define weak void @f.weak()
define weak void @f.weak() {
  ret void
}

; MODULE: declare extern_weak void @f.extern_weak()
declare extern_weak void @f.extern_weak()

; MODULE: define linkonce_odr void @f.linkonce_odr()
define linkonce_odr void @f.linkonce_odr() {
  ret void
}

; MODULE: define weak_odr void @f.weak_odr()
define weak_odr void @f.weak_odr() {
  ret void
}

; MODULE: define void @f.external()
define void @f.external() {
  ret void
}

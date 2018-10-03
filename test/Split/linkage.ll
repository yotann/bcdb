; RUN: llvm-as < %s | bc-split - %t
; RUN: llvm-dis < %t/functions/f.private | FileCheck --check-prefix=PRIVATE %s
; RUN: llvm-dis < %t/remainder/module | FileCheck --check-prefix=MODULE %s
; RUN: FileCheck --check-prefix=LINKAGE -match-full-lines %s < %t/linkage.txt

; MODULE: declare void @f.private()
; PRIVATE: define void @0()
; LINKAGE: f.private 9
define private void @f.private() {
  ret void
}

; MODULE: declare void @f.internal()
; LINKAGE: f.internal 3
define internal void @f.internal() {
  ret void
}

; MODULE: declare void @f.available_externally()
; LINKAGE: f.available_externally 12
define available_externally void @f.available_externally() {
  ret void
}

; MODULE: declare void @f.linkonce()
; LINKAGE: f.linkonce 18
define linkonce void @f.linkonce() {
  ret void
}

; MODULE: declare void @f.weak()
; LINKAGE: f.weak 16
define weak void @f.weak() {
  ret void
}

; MODULE: declare extern_weak void @f.extern_weak()
; LINKAGE-NOT: f.extern_weak
declare extern_weak void @f.extern_weak()

; MODULE: declare void @f.linkonce_odr()
; LINKAGE: f.linkonce_odr 19
define linkonce_odr void @f.linkonce_odr() {
  ret void
}

; MODULE: declare void @f.weak_odr()
; LINKAGE: f.weak_odr 17
define weak_odr void @f.weak_odr() {
  ret void
}

; MODULE: declare void @f.external()
; LINKAGE: f.external 0
define external void @f.external() {
  ret void
}

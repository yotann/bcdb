; RUN: llvm-as < %s | bc-split -o %t
; RUN: llvm-dis < %t/functions/f.dllexport | FileCheck --check-prefix=EXPORT %s
; RUN: llvm-dis < %t/remainder/module | FileCheck --check-prefix=MODULE %s

; MODULE: define dllexport void @f.dllexport()
; MODULE-NEXT: unreachable
; EXPORT: define void @0()
define dllexport void @f.dllexport() {
  call void @f.dllimport()
  ret void
}

; MODULE: declare dllimport void @f.dllimport()
; EXPORT: declare void @f.dllimport()
declare dllimport void @f.dllimport()

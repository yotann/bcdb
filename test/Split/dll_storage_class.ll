; RUN: llvm-as < %s | bc-split - %t
; RUN: llvm-dis < %t/functions/f.dllexport | FileCheck --check-prefix=EXPORT %s
; RUN: llvm-dis < %t/remainder/module | FileCheck --check-prefix=MODULE %s

; MODULE: declare dllexport void @f.dllexport()
; EXPORT: define void @0()
define dllexport void @f.dllexport() {
  call void @f.dllimport()
  ret void
}

; EXPORT: declare void @f.dllimport()
declare dllimport void @f.dllimport()

; RUN: llvm-as < %s | bc-split -o %t
; RUN: llvm-dis < %t/functions/f.dllexport | FileCheck --check-prefix=DEFINE %s
; RUN: llvm-dis < %t/remainder/module | FileCheck --check-prefix=MODULE %s
; RUN: bc-join %t | llvm-dis          | FileCheck --check-prefix=JOINED %s

; JOINED: declare dllimport void @f.dllimport()
; JOINED: define dllexport void @f.dllexport()

; MODULE: define dllexport void @f.dllexport()
; MODULE-NEXT: unreachable
; DEFINE: define void @0()
define dllexport void @f.dllexport() {
  call void @f.dllimport()
  ret void
}

; MODULE: declare dllimport void @f.dllimport()
; DEFINE: declare void @f.dllimport()
declare dllimport void @f.dllimport()

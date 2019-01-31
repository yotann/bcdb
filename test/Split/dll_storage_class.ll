; RUN: llvm-as < %s | bc-split -o %t
; RUN: llvm-dis < %t/functions/f.dllexport.bc | FileCheck --check-prefix=DEFINE %s
; RUN: llvm-dis < %t/remainder/module.bc      | FileCheck --check-prefix=MODULE %s
; RUN: bc-join %t | llvm-dis                  | FileCheck --check-prefix=JOINED %s

; MODULE: define dllexport void @f.dllexport()
; MODULE-NEXT: unreachable
; DEFINE: define void @0()
; JOINED: define dllexport void @f.dllexport()
define dllexport void @f.dllexport() {
  call void @f.dllimport()
  ret void
}

; MODULE: declare dllimport void @f.dllimport()
; DEFINE: declare void @f.dllimport()
; JOINED: declare dllimport void @f.dllimport()
declare dllimport void @f.dllimport()

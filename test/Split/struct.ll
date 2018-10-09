; RUN: llvm-as < %s | bc-split - %t
; RUN: llvm-dis < %t/functions/f | FileCheck --check-prefix=F %s
; RUN: llvm-dis < %t/functions/g | FileCheck --check-prefix=G %s
; RUN: llvm-dis < %t/remainder/module | FileCheck --check-prefix=MODULE %s

; F: %mytype = type { %needed*, %mytype*, %unneeded*, %needed3* }
; G-NOT: %mytype
; MODULE: %mytype = type { %needed*, %mytype*, %unneeded*, %needed3* }
%mytype = type { %needed*, %mytype*, %unneeded*, %needed3* }

; F: %needed = type { %needed2* }
%needed = type { %needed2* }

; F: %needed2 = type { i32 }
%needed2 = type { i32 }

; F: %unneeded = type opaque
; MODULE: %unneeded = type { i64 }
%unneeded = type { i64 }

; F: %needed3 = type { i8 }
%needed3 = type { i8 }

; F: %needed4 = type { i16 }
%needed4 = type { i16 }

define void @f(%mytype*, %needed4) personality void(i32)* @g {
  %n1p = getelementptr %mytype, %mytype* %0, i64 0, i32 0
  %n1 = load %needed*, %needed** %n1p
  %n2p = getelementptr %needed, %needed* %n1, i64 0, i32 0
  %n2 = load %needed2*, %needed2** %n2p
  %n2x = getelementptr %needed2, %needed2* %n2, i64 0, i32 0
  %up = getelementptr %mytype, %mytype* %0, i64 0, i32 2
  %u = load %unneeded*, %unneeded** %up
  %n3pp = getelementptr %mytype, %mytype* %0, i64 0, i32 3
  %n3p = load %needed3*, %needed3** %n3pp
  %n3 = load %needed3, %needed3* %n3p
  ret void
}

declare void @g(i32)

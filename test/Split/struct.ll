; RUN: llvm-as < %s | bc-split -o %t
; RUN: llvm-dis < %t/functions/f.bc      | FileCheck --check-prefix=F %s
; RUN: llvm-dis < %t/functions/g.bc      | FileCheck --check-prefix=G %s
; RUN: llvm-dis < %t/remainder/module.bc | FileCheck --check-prefix=MODULE %s
; RUN: bc-join %t | llvm-dis             | FileCheck --check-prefix=JOINED %s

; F: %0 = type { %1*, %0*, %3*, %4* }
; G-NOT: %0 = type
; MODULE: %mytype = type { %needed*, %mytype*, %unneeded*, %needed3* }
; JOINED: %mytype = type { %needed*, %mytype*, %unneeded*, %needed3* }
%mytype = type { %needed*, %mytype*, %unneeded*, %needed3* }

; F: %1 = type { %2* }
%needed = type { %needed2* }

; F: %2 = type { i32 }
%needed2 = type { i32 }

; MODULE: %unneeded = type { i64 }
; JOINED: %unneeded = type { i64 }
; F: %3 = type opaque
%unneeded = type { i64 }

; F: %4 = type { i8 }
%needed3 = type { i8 }

; F: %5 = type { i16 }
%needed4 = type { i16 }

; JOINED: define void @f(%mytype* %arg0, %needed4 %arg1)
define void @f(%mytype* %arg0, %needed4 %arg1) personality void(i32)* @g {
  %n1p = getelementptr %mytype, %mytype* %arg0, i64 0, i32 0
  %n1 = load %needed*, %needed** %n1p
  %n2p = getelementptr %needed, %needed* %n1, i64 0, i32 0
  %n2 = load %needed2*, %needed2** %n2p
  %n2x = getelementptr %needed2, %needed2* %n2, i64 0, i32 0
  %up = getelementptr %mytype, %mytype* %arg0, i64 0, i32 2
  %u = load %unneeded*, %unneeded** %up
  %n3pp = getelementptr %mytype, %mytype* %arg0, i64 0, i32 3
  %n3p = load %needed3*, %needed3** %n3pp
  %n3 = load %needed3, %needed3* %n3p
  ret void
}

define void @g(i32) {
  ret void
}

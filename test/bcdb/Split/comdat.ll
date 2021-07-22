; RUN: llvm-as < %s | bc-split -o %t
; RUN: llvm-dis < %t/functions/f.any.bc  | FileCheck --check-prefix=DEFINE -match-full-lines %s
; RUN: llvm-dis < %t/remainder/module.bc | FileCheck --check-prefix=MODULE %s
; RUN: bc-join %t | llvm-dis             | FileCheck --check-prefix=JOINED %s

; MODULE: $any = comdat any
; MODULE: $exactmatch = comdat exactmatch
; MODULE: $largest = comdat largest
; MODULE: $noduplicates = comdat noduplicates
; MODULE: $samesize = comdat samesize

; JOINED: $any = comdat any
; JOINED: $exactmatch = comdat exactmatch
; JOINED: $largest = comdat largest
; JOINED: $noduplicates = comdat noduplicates
; JOINED: $samesize = comdat samesize
; JOINED: define void @f.any() comdat($any)
; JOINED: define void @f.exactmatch() comdat($exactmatch)
; JOINED: define void @f.largest() comdat($largest)
; JOINED: define void @f.noduplicates() comdat($noduplicates)
; JOINED: define void @f.samesize() comdat($samesize)

$any = comdat any
$exactmatch = comdat exactmatch
$largest = comdat largest
$noduplicates = comdat noduplicates
$samesize = comdat samesize

; MODULE: define void @f.any() comdat($any)
; MODULE-NEXT: unreachable
; DEFINE: define void @0() {
define void @f.any() comdat($any) {
  call void @f.exactmatch()
  ret void
}

; MODULE: define void @f.exactmatch() comdat($exactmatch)
; DEFINE: declare void @f.exactmatch()
define void @f.exactmatch() comdat($exactmatch) {
  ret void
}

; MODULE: define void @f.largest() comdat($largest)
define void @f.largest() comdat($largest) {
  ret void
}

; MODULE: define void @f.noduplicates() comdat($noduplicates)
define void @f.noduplicates() comdat($noduplicates) {
  ret void
}

; MODULE: define void @f.samesize() comdat($samesize)
define void @f.samesize() comdat($samesize) {
  ret void
}

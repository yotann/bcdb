; RUN: llvm-as < %s | bc-split - %t
; RUN: llvm-dis < %t/functions/f.any | FileCheck --check-prefix=DEFINE -match-full-lines %s
; RUN: llvm-dis < %t/remainder/module | FileCheck --check-prefix=MODULE %s

; MODULE: $any = comdat any
$any = comdat any
; MODULE: $exactmatch = comdat exactmatch
$exactmatch = comdat exactmatch
; MODULE: $largest = comdat largest
$largest = comdat largest
; MODULE: $noduplicates = comdat noduplicates
$noduplicates = comdat noduplicates
; MODULE: $samesize = comdat samesize
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

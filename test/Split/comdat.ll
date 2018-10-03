; RUN: llvm-as < %s | bc-split - %t
; RUN: llvm-dis < %t/functions/f.any | FileCheck --check-prefix=DEFINE -match-full-lines %s
; RUN: llvm-dis < %t/remainder/module | FileCheck --check-prefix=MODULE %s
; RUN: FileCheck --check-prefix=COMDAT -match-full-lines %s < %t/comdat.txt

$any = comdat any
$exactmatch = comdat exactmatch
$largest = comdat largest
$noduplicates = comdat noduplicates
$samesize = comdat samesize

; COMDAT-NOT: f {{.*}}
define void @f() {
  ret void
}

; DEFINE: define void @0() {
; MODULE: declare void @f.any()
; COMDAT: f.any 1 any
define void @f.any() comdat($any) {
  call void @f.exactmatch()
  ret void
}

; DEFINE: declare void @f.exactmatch()
; COMDAT: f.exactmatch 2 exactmatch
define void @f.exactmatch() comdat($exactmatch) {
  ret void
}

; COMDAT: f.largest 3 largest
define void @f.largest() comdat($largest) {
  ret void
}

; COMDAT: f.noduplicates 4 noduplicates
define void @f.noduplicates() comdat($noduplicates) {
  ret void
}

; COMDAT: f.samesize 5 samesize
define void @f.samesize() comdat($samesize) {
  ret void
}

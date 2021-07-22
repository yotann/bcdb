; RUN: llvm-as < %s            | llvm-dis > %t1
; RUN: llvm-as < %s | bc-align | llvm-dis > %t2
; RUN: diff %t1 %t2

define void @f(i8** nocapture %ptr1) {
entry:
  br label %here.i

here.i:
  store i8* blockaddress(@doit, %here), i8** %ptr1, align 8
  br label %doit.exit

doit.exit:
  ret void
}

define void @doit(i8** nocapture %pptr) {
entry:
  br label %here

here:
  store i8* blockaddress(@doit, %here), i8** %pptr, align 8
  br label %end

end:
  ret void
}

define void @doitagain(i8** nocapture %pptr) {
entry:
  br label %here

here:
  store i8* blockaddress(@doit, %here), i8** %pptr, align 8
  br label %end

end:
  ret void
}

; Check a blockaddress taken in two separate functions before the referenced
; function.
define i8* @take1() {
  ret i8* blockaddress(@taken, %bb)
}
define i8* @take2() {
  ret i8* blockaddress(@taken, %bb)
}
define void @taken() {
  unreachable
bb:
  unreachable
}

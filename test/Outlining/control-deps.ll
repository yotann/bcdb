; RUN: opt -load %shlibdir/BCDBOutliningPlugin%shlibext \
; RUN:     -outlining-dependence -analyze %s | FileCheck %s

; RUN: opt -load %shlibdir/BCDBOutliningPlugin%shlibext \
; RUN:     -outlining-extractor -outline-unprofitable -verify -S %s



; CHECK-LABEL: define void @f.if.else
define void @f.if.else() {
; CHECK: entry:
; CHECK-NEXT: block 0 depends [] forced []
; CHECK-NEXT: node 1 depends [0] forced [1, 2, 3, 4, 5]
; CHECK-NEXT: br i1 undef, label %then, label %else
entry:
  br i1 undef, label %then, label %else

; CHECK: then:
; CHECK-NEXT: block 2 depends [0, 1] forced []
; CHECK-NEXT: node 3 depends [0, 1, 2] forced []
; CHECK-NEXT: br label %endif
then:
  br label %endif

; CHECK: else:
; CHECK-NEXT: block 4 depends [0, 1] forced []
; CHECK-NEXT: node 5 depends [0, 1, 4] forced []
; CHECK-NEXT: br label %endif
else:
  br label %endif

; CHECK: endif:
; CHECK-NEXT: block 6 depends [] forced []
; CHECK-NEXT: node 7 prevents outlining
; CHECK-NEXT: ret void
endif:
  ret void
}



; CHECK-LABEL: define void @f.while
define void @f.while() {
; CHECK: entry:
; CHECK-NEXT: block 0 depends [] forced []
; CHECK-NEXT: node 1 depends [0] forced []
; CHECK-NEXT: br label %head
entry:
  br label %head

; CHECK: head:
; CHECK-NEXT: block 2 depends [] forced [2, 3, 4, 5, 6, 7, 8, 9]
; CHECK-NEXT: node 3 depends [2] forced [2, 3, 4, 5, 6, 7, 8, 9]
; CHECK-NEXT: br i1 undef, label %may_break, label %exit
head:
  br i1 undef, label %may_break, label %exit

; CHECK: may_break:
; CHECK-NEXT: block 4 depends [2, 3] forced []
; CHECK-NEXT: node 5 depends [2, 3, 4] forced [2, 3, 4, 5, 6, 7, 8, 9]
; CHECK-NEXT: br i1 undef, label %exit, label %may_continue
may_break:
  br i1 undef, label %exit, label %may_continue

; CHECK: may_continue:
; CHECK-NEXT: block 6 depends [2, 3, 4, 5] forced []
; CHECK-NEXT: node 7 depends [2, 3, 4, 5, 6] forced [7, 8, 9]
; CHECK-NEXT: br i1 undef, label %head, label %body
may_continue:
  br i1 undef, label %head, label %body

; CHECK: body:
; CHECK-NEXT: block 8 depends [2, 3, 4, 5, 6, 7] forced []
; CHECK-NEXT: node 9 depends [2, 3, 4, 5, 6, 7, 8] forced []
; CHECK-NEXT: br label %head
body:
  br label %head

; CHECK: exit:
; CHECK-NEXT: block 10 depends [] forced []
; CHECK-NEXT: node 11 prevents outlining
; CHECK-NEXT: ret void
exit:
  ret void
}



; CHECK-LABEL: define void @f.do.while
define void @f.do.while() {
; CHECK: entry:
; CHECK-NEXT: block 0 depends [] forced []
; CHECK-NEXT: node 1 depends [0] forced []
; CHECK-NEXT: br label %may_continue
entry:
  br label %may_continue

; CHECK: may_continue:
; CHECK-NEXT: block 2 depends [] forced [2, 3, 4, 5, 6, 7, 8, 9]
; CHECK-NEXT: node 3 depends [2] forced [2, 3, 4, 5, 6, 7, 8, 9]
; CHECK-NEXT: br i1 undef, label %head, label %may_break
may_continue:
  br i1 undef, label %head, label %may_break

; CHECK: head:
; CHECK-NEXT: block 4 depends [2, 3] forced [2, 3, 4, 5, 6, 7, 8, 9]
; CHECK-NEXT: node 5 depends [2, 3, 4] forced [2, 3, 4, 5, 6, 7, 8, 9]
; CHECK-NEXT: br i1 undef, label %may_continue, label %exit
head:
  br i1 undef, label %may_continue, label %exit

; CHECK: may_break:
; CHECK-NEXT: block 6 depends [2, 3] forced []
; CHECK-NEXT: node 7 depends [2, 3, 6] forced [2, 3, 4, 5, 6, 7, 8, 9]
; CHECK-NEXT: br i1 undef, label %exit, label %body
may_break:
  br i1 undef, label %exit, label %body

; CHECK: body:
; CHECK-NEXT: block 8 depends [2, 3, 6, 7] forced []
; CHECK-NEXT: node 9 depends [2, 3, 6, 7, 8] forced []
; CHECK-NEXT: br label %head
body:
  br label %head

; CHECK: exit:
; CHECK-NEXT: block 10 depends [] forced []
; CHECK-NEXT: node 11 prevents outlining
; CHECK-NEXT: ret void
exit:
  ret void
}



; CHECK-LABEL: define void @f.irreducible
define void @f.irreducible() {
; CHECK: entry:
; CHECK-NEXT: block 0 depends [] forced []
; CHECK-NEXT: node 1 depends [0] forced [1, 2, 3, 4, 5]
; CHECK-NEXT: br i1 undef, label %left, label %right
entry:
  br i1 undef, label %left, label %right

; CHECK: left:
; CHECK-NEXT: block 2 depends [0, 1] forced [1, 2, 3, 4, 5]
; CHECK-NEXT: node 3 depends [0, 1, 2] forced [1, 2, 3, 4, 5]
; CHECK-NEXT: br i1 undef, label %exit, label %right
left:
  br i1 undef, label %exit, label %right

; CHECK: right:
; CHECK-NEXT: block 4 depends [0, 1] forced [1, 2, 3, 4, 5]
; CHECK-NEXT: node 5 depends [0, 1, 4] forced [1, 2, 3, 4, 5]
; CHECK-NEXT: br i1 undef, label %left, label %exit
right:
  br i1 undef, label %left, label %exit

; CHECK: exit:
; CHECK-NEXT: block 6 depends [] forced []
; CHECK-NEXT: node 7 prevents outlining
; CHECK-NEXT: ret void
exit:
  ret void
}

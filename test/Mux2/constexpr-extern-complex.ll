; RUN: bcdb init -uri sqlite:%t.bcdb
; RUN: bcdb add -uri sqlite:%t.bcdb %s -name prog
; RUN: bcdb mux2 -uri sqlite:%t.bcdb prog -o %t --muxed-name=libmuxed.so --disable-opts

@x = extern_weak global i8, align 8

define void @f() {
  and i32 undef, xor (i32 lshr (i32 ptrtoint (i8* @x to i32), i32 4), i32 lshr (i32 ptrtoint (i8* @x to i32), i32 9))
  ret void
}

define i32 @main() {
  ret i32 0
}

!llvm.module.flags = !{!1}
!1 = !{i32 2, !"bcdb.elf.type", i32 2}

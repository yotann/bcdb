// RUN: %mux2test %s %t
// RUN: %t.elf/module

declare void @exit()

define i32 @main(i32, i8**) {
  call void bitcast (void ()* @exit to void (i32)*)(i32 0)
  ret i32 1
}

!llvm.module.flags = !{!1}
!1 = !{i32 2, !"bcdb.elf.type", i32 2}

// RUN: %mux2test %s %t
// RUN: %t.elf/module

define i32 @main() {
  ret i32 0
}

!llvm.module.flags = !{!1}
!1 = !{i32 2, !"bcdb.elf.type", i32 2}

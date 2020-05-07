// RUN: %mux2test %s %t
// RUN: %t.elf/module
// RUN: %mux2test %s %t --allow-spurious-exports
// RUN: %t.elf/module
// RUN: %mux2test %s %t --allow-spurious-exports --known-dynamic-defs
// RUN: %t.elf/module
// RUN: %mux2test %s %t --allow-spurious-exports --known-dynamic-defs --known-dynamic-uses --known-rtld-local
// RUN: %t.elf/module

define i32 @main(i32, i8**) {
  %x = tail call fastcc i32 @func()
  ret i32 %x
}

define internal fastcc i32 @func() unnamed_addr {
  ret i32 0
}

!llvm.module.flags = !{!1}
!1 = !{i32 2, !"bcdb.elf.type", i32 2}

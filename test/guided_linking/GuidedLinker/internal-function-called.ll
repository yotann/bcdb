// RUN: %gltest %s %t
// RUN: %t.elf/module
// RUN: %gltest %s %t --noweak
// RUN: %t.elf/module
// RUN: %gltest %s %t --noweak --nooverride
// RUN: %t.elf/module
// RUN: %gltest %s %t --noweak --nooverride --nouse --noplugin
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

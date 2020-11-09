// RUN: %gltest %s %t
// RUN: %t.elf/module

@ptr_exit = internal constant void (i32)* @exit, align 8

declare void @exit(i32)

define i32 @main() {
  %exit = load volatile void (i32)*, void (i32)** @ptr_exit, align 8
  call void %exit(i32 0)
  ret i32 1
}

!llvm.module.flags = !{!1}
!1 = !{i32 2, !"bcdb.elf.type", i32 2}

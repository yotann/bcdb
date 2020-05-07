// RUN: %mux2test %s %t --weak-library
// RUN: %t.elf/module
// RUN: %mux2test %s %t --allow-spurious-exports
// RUN: %t.elf/module
// RUN: %mux2test %s %t --allow-spurious-exports --known-dynamic-defs --weak-library
// RUN: %t.elf/module
// RUN: %mux2test %s %t --allow-spurious-exports --known-dynamic-defs --known-dynamic-uses --known-rtld-local
// RUN: %t.elf/module

@global = internal global void()* @func

declare void @exit(i32)

define internal void @func() {
  call void @exit(i32 0)
  unreachable
}

define i32 @main(i32, i8**) {
  %f = load void()*, void()** @global
  call void %f()
  ret i32 1
}

!llvm.module.flags = !{!1}
!1 = !{i32 2, !"bcdb.elf.type", i32 2}

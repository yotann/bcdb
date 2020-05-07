// RUN: %mux2test %s %t
// RUN: %t.elf/module1
// RUN: %mux2test %s %t --allow-spurious-exports
// RUN: %t.elf/module1
// RUN: %mux2test %s %t --allow-spurious-exports --known-dynamic-defs
// RUN: %t.elf/module1
// RUN: %mux2test %s %t --allow-spurious-exports --known-dynamic-defs --known-dynamic-uses --known-rtld-local
// RUN: %t.elf/module1



#if MODULE0

@llvm.global_ctors = appending global [1 x { i32, void ()*, i8* }] [{ i32, void ()*, i8* } { i32 65535, void ()* @ctor, i8* null }]

declare void @exit(i32)

define internal void @ctor() {
  call void @exit(i32 1)
  unreachable
}

!llvm.module.flags = !{!1}
!1 = !{i32 2, !"bcdb.elf.type", i32 3}



#elif MODULE1

define i32 @main(i32, i8**) {
  ret i32 0
}

!llvm.module.flags = !{!1}
!1 = !{i32 2, !"bcdb.elf.type", i32 2}



#endif

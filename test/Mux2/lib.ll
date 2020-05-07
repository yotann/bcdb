// RUN: %mux2test %s %t
// RUN: %t.elf/module1
// RUN: %mux2test %s %t --allow-spurious-exports
// RUN: %t.elf/module1
// RUN: %mux2test %s %t --allow-spurious-exports --known-dynamic-defs
// RUN: %t.elf/module1
// RUN: %mux2test %s %t --allow-spurious-exports --known-dynamic-defs --known-dynamic-uses --known-rtld-local
// RUN: %t.elf/module1

#if MODULE0

@global = global i32 1

define void @set_global_to_0() {
  store i32 0, i32* @global
  ret void
}

!llvm.module.flags = !{!1}
!1 = !{i32 2, !"bcdb.elf.type", i32 3}



#elif MODULE1

@global = external global i32

declare void @set_global_to_0()

define i32 @main(i32, i8**) {
  call void @set_global_to_0()
  %x = load i32, i32* @global
  ret i32 %x
}

!llvm.module.flags = !{!1, !2}
!1 = !{i32 2, !"bcdb.elf.type", i32 2}
!2 = !{i32 6, !"bcdb.elf.needed", !3}
!3 = !{!"module0"}



#endif
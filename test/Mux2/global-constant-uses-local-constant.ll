// RUN: %mux2test %s %t --allow-spurious-exports --known-dynamic-defs -O1
// RUN: %t.elf/module | FileCheck %s

// CHECK: 789
// CHECK: 789

@value = private unnamed_addr constant [4 x i8] c"789\00", align 1
@pointer = local_unnamed_addr constant i8* getelementptr inbounds ([4 x i8], [4 x i8]* @value, i32 0, i32 0)

declare i32 @puts(i8*)

define i8* @get_value() local_unnamed_addr {
  %p = load i8*, i8** @pointer
  call i32 @puts(i8* %p)
  ret i8* %p
}

define i32 @main() {
  %p = call i8* @get_value()
  call i32 @puts(i8* %p)
  ret i32 0
}

!llvm.module.flags = !{!1}
!1 = !{i32 2, !"bcdb.elf.type", i32 2}

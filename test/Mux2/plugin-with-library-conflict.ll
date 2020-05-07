// RUN: %mux2test %s %t
// RUN: %t.elf/module3 %t.elf/module2
// RUN: %mux2test %s %t --allow-spurious-exports
// RUN: %t.elf/module3 %t.elf/module2
// RUN: %mux2test %s %t --allow-spurious-exports --known-dynamic-defs
// RUN: %t.elf/module3 %t.elf/module2



#if MODULE0

declare void @exit(i32)

define i32 @using_history(i32 %x) local_unnamed_addr {
  call void @exit(i32 1)
  unreachable
}

!llvm.module.flags = !{!1, !2}
!1 = !{i32 2, !"bcdb.elf.type", i32 3}
!2 = !{i32 7, !"PIC Level", i32 2}



#elif MODULE1

@rl_readline_version = local_unnamed_addr global i32 1539, align 4

define i32 @using_history(i32 %x) local_unnamed_addr {
  %y = sub i32 %x, 1539
  ret i32 %y
}

!llvm.module.flags = !{!1, !2}
!1 = !{i32 2, !"bcdb.elf.type", i32 3}
!2 = !{i32 7, !"PIC Level", i32 2}



#elif MODULE2

@rl_readline_version = external local_unnamed_addr global i32, align 4

declare i32 @using_history(i32) local_unnamed_addr

define i32 @PyInit_readline() local_unnamed_addr {
  %x = load i32, i32* @rl_readline_version
  %y = tail call i32 @using_history(i32 %x)
  ret i32 %y
}

!llvm.module.flags = !{!1, !2, !3}
!1 = !{i32 2, !"bcdb.elf.type", i32 3}
!2 = !{i32 7, !"PIC Level", i32 2}
!3 = !{i32 6, !"bcdb.elf.needed", !4}
!4 = !{!"module1"}



#elif MODULE3

@name = private unnamed_addr constant [16 x i8] c"PyInit_readline\00", align 1

declare dso_local i8* @dlopen(i8*, i32) local_unnamed_addr

declare dso_local i8* @dlsym(i8*, i8*) local_unnamed_addr

define dso_local i32 @main(i32 %0, i8** nocapture readonly %1) local_unnamed_addr {
  %3 = getelementptr inbounds i8*, i8** %1, i64 1
  %4 = load i8*, i8** %3, align 8
  %5 = tail call i8* @dlopen(i8* %4, i32 2)
  %6 = tail call i8* @dlsym(i8* %5, i8* getelementptr inbounds ([16 x i8], [16 x i8]* @name, i64 0, i64 0))
  %7 = bitcast i8* %6 to i32 ()*
  %8 = tail call i32 %7()
  ret i32 %8
}

!llvm.module.flags = !{!1, !2}
!1 = !{i32 2, !"bcdb.elf.type", i32 2}
!2 = !{i32 6, !"bcdb.elf.needed", !3}
!3 = !{!"libdl.so"}



#endif

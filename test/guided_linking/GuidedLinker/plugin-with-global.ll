// RUN: %gltest %s %t
// RUN: %t.elf/module1 %t.elf/module0
// RUN: %gltest %s %t --noweak
// RUN: %t.elf/module1 %t.elf/module0
// RUN: %gltest %s %t --noweak --nooverride
// RUN: %t.elf/module1 %t.elf/module0



#if MODULE0

@global = local_unnamed_addr global i32 3

define void @func(i32 %x) local_unnamed_addr {
  store i32 %x, i32* @global
  ret void
}

!llvm.module.flags = !{!1, !2}
!1 = !{i32 2, !"bcdb.elf.type", i32 3}
!2 = !{i32 7, !"PIC Level", i32 2}



#elif MODULE1

@global_name = private unnamed_addr constant [7 x i8] c"global\00", align 1
@func_name = private unnamed_addr constant [5 x i8] c"func\00", align 1

declare dso_local i8* @dlopen(i8*, i32) local_unnamed_addr

declare dso_local i8* @dlsym(i8*, i8*) local_unnamed_addr

define dso_local i32 @main(i32 %argc, i8** nocapture readonly %argv) local_unnamed_addr {
  %argv1 = getelementptr inbounds i8*, i8** %argv, i64 1
  %arg = load i8*, i8** %argv1, align 8
  %handle = tail call i8* @dlopen(i8* %arg, i32 2)
  %globalp = tail call i8* @dlsym(i8* %handle, i8* getelementptr inbounds ([7 x i8], [7 x i8]* @global_name, i64 0, i64 0))
  %global = bitcast i8* %globalp to i32*
  %funcp = tail call i8* @dlsym(i8* %handle, i8* getelementptr inbounds ([5 x i8], [5 x i8]* @func_name, i64 0, i64 0))
  %func = bitcast i8* %funcp to void (i32)*
  call void %func(i32 7)
  %load = load i32, i32* %global
  %result = sub i32 %load, 7
  ret i32 %result
}

!llvm.module.flags = !{!1, !2}
!1 = !{i32 2, !"bcdb.elf.type", i32 2}
!2 = !{i32 6, !"bcdb.elf.needed", !3}
!3 = !{!"libdl.so"}



#endif

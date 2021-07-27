; XFAIL: *
; Fails because "opt --outlining-dependence" has more precise MemorySSA results
; than "opt --outlining-extractor" for some reason, and candidates that are
; valid for one are invalid for the other. This problem doesn't happen with the
; new pass manager.

; REQUIRES: x86_64

; RUN: %outliningtest --no-run %s

source_filename = "build-llvm12/SingleSource/Regression/C/gcc-c-torture/execute/ieee/GCC-C-execute-ieee-copysign2"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@Zl = internal constant [8 x x86_fp80] [x86_fp80 0xK3FFF8000000000000000, x86_fp80 0xKBFFF8000000000000000, x86_fp80 0xKBFFF8000000000000000, x86_fp80 0xK80000000000000000000, x86_fp80 0xK80000000000000000000, x86_fp80 0xK00000000000000000000, x86_fp80 0xKFFFF8000000000000000, x86_fp80 0xK7FFFC000000000000000], align 16

; Function Attrs: nounwind uwtable
define dso_local void @testf() local_unnamed_addr #0 {
  ret void
}

; Function Attrs: nounwind uwtable
define dso_local void @test() local_unnamed_addr #0 {
  ret void
}

; Function Attrs: nounwind uwtable
define dso_local void @testl() local_unnamed_addr #0 {
  %1 = alloca [8 x x86_fp80], align 16
  %2 = bitcast [8 x x86_fp80]* %1 to i8*
  call void @llvm.lifetime.start.p0i8(i64 128, i8* nonnull %2) #5
  %3 = getelementptr inbounds [8 x x86_fp80], [8 x x86_fp80]* %1, i64 0, i64 0
  store x86_fp80 0xK3FFF8000000000000000, x86_fp80* %3, align 16, !tbaa !7
  %4 = getelementptr inbounds [8 x x86_fp80], [8 x x86_fp80]* %1, i64 0, i64 1
  store x86_fp80 0xKBFFF8000000000000000, x86_fp80* %4, align 16, !tbaa !7
  %5 = tail call x86_fp80 @llvm.fabs.f80(x86_fp80 0xKBFFF8000000000000000)
  %6 = fneg x86_fp80 %5
  %7 = getelementptr inbounds [8 x x86_fp80], [8 x x86_fp80]* %1, i64 0, i64 2
  store x86_fp80 %6, x86_fp80* %7, align 16, !tbaa !7
  %8 = getelementptr inbounds [8 x x86_fp80], [8 x x86_fp80]* %1, i64 0, i64 3
  store x86_fp80 0xK80000000000000000000, x86_fp80* %8, align 16, !tbaa !7
  %9 = tail call x86_fp80 @llvm.fabs.f80(x86_fp80 0xK80000000000000000000)
  %10 = fneg x86_fp80 %9
  %11 = getelementptr inbounds [8 x x86_fp80], [8 x x86_fp80]* %1, i64 0, i64 4
  store x86_fp80 %10, x86_fp80* %11, align 16, !tbaa !7
  %12 = getelementptr inbounds [8 x x86_fp80], [8 x x86_fp80]* %1, i64 0, i64 5
  store x86_fp80 %9, x86_fp80* %12, align 16, !tbaa !7
  %13 = getelementptr inbounds [8 x x86_fp80], [8 x x86_fp80]* %1, i64 0, i64 6
  store x86_fp80 0xKFFFF8000000000000000, x86_fp80* %13, align 16, !tbaa !7
  %14 = tail call x86_fp80 @llvm.fabs.f80(x86_fp80 0xKFFFFC000000000000000)
  %15 = getelementptr inbounds [8 x x86_fp80], [8 x x86_fp80]* %1, i64 0, i64 7
  store x86_fp80 %14, x86_fp80* %15, align 16, !tbaa !7
  %16 = bitcast [8 x x86_fp80]* %1 to i8*
  %17 = call i32 @bcmp(i8* nonnull dereferenceable(10) %16, i8* nonnull dereferenceable(10) bitcast ([8 x x86_fp80]* @Zl to i8*), i64 10)
  %18 = icmp eq i32 %17, 0
  br i1 %18, label %19, label %23

19:                                               ; preds = %0
  %20 = bitcast x86_fp80* %4 to i8*
  %21 = call i32 @bcmp(i8* nonnull dereferenceable(10) %20, i8* nonnull dereferenceable(10) bitcast (x86_fp80* getelementptr inbounds ([8 x x86_fp80], [8 x x86_fp80]* @Zl, i64 0, i64 1) to i8*), i64 10)
  %22 = icmp eq i32 %21, 0
  br i1 %22, label %24, label %23

23:                                               ; preds = %44, %40, %36, %32, %28, %24, %19, %0
  tail call void @abort() #6
  unreachable

24:                                               ; preds = %19
  %25 = bitcast x86_fp80* %7 to i8*
  %26 = call i32 @bcmp(i8* nonnull dereferenceable(10) %25, i8* nonnull dereferenceable(10) bitcast (x86_fp80* getelementptr inbounds ([8 x x86_fp80], [8 x x86_fp80]* @Zl, i64 0, i64 2) to i8*), i64 10)
  %27 = icmp eq i32 %26, 0
  br i1 %27, label %28, label %23

28:                                               ; preds = %24
  %29 = bitcast x86_fp80* %8 to i8*
  %30 = call i32 @bcmp(i8* nonnull dereferenceable(10) %29, i8* nonnull dereferenceable(10) bitcast (x86_fp80* getelementptr inbounds ([8 x x86_fp80], [8 x x86_fp80]* @Zl, i64 0, i64 3) to i8*), i64 10)
  %31 = icmp eq i32 %30, 0
  br i1 %31, label %32, label %23

32:                                               ; preds = %28
  %33 = bitcast x86_fp80* %11 to i8*
  %34 = call i32 @bcmp(i8* nonnull dereferenceable(10) %33, i8* nonnull dereferenceable(10) bitcast (x86_fp80* getelementptr inbounds ([8 x x86_fp80], [8 x x86_fp80]* @Zl, i64 0, i64 4) to i8*), i64 10)
  %35 = icmp eq i32 %34, 0
  br i1 %35, label %36, label %23

36:                                               ; preds = %32
  %37 = bitcast x86_fp80* %12 to i8*
  %38 = call i32 @bcmp(i8* nonnull dereferenceable(10) %37, i8* nonnull dereferenceable(10) bitcast (x86_fp80* getelementptr inbounds ([8 x x86_fp80], [8 x x86_fp80]* @Zl, i64 0, i64 5) to i8*), i64 10)
  %39 = icmp eq i32 %38, 0
  br i1 %39, label %40, label %23

40:                                               ; preds = %36
  %41 = bitcast x86_fp80* %13 to i8*
  %42 = call i32 @bcmp(i8* nonnull dereferenceable(10) %41, i8* nonnull dereferenceable(10) bitcast (x86_fp80* getelementptr inbounds ([8 x x86_fp80], [8 x x86_fp80]* @Zl, i64 0, i64 6) to i8*), i64 10)
  %43 = icmp eq i32 %42, 0
  br i1 %43, label %44, label %23

44:                                               ; preds = %40
  %45 = bitcast x86_fp80* %15 to i8*
  %46 = call i32 @bcmp(i8* nonnull dereferenceable(10) %45, i8* nonnull dereferenceable(10) bitcast (x86_fp80* getelementptr inbounds ([8 x x86_fp80], [8 x x86_fp80]* @Zl, i64 0, i64 7) to i8*), i64 10)
  %47 = icmp eq i32 %46, 0
  br i1 %47, label %48, label %23

48:                                               ; preds = %44
  call void @llvm.lifetime.end.p0i8(i64 128, i8* nonnull %2) #5
  ret void
}

; Function Attrs: argmemonly nofree nosync nounwind willreturn
declare void @llvm.lifetime.start.p0i8(i64 immarg, i8* nocapture) #1

; Function Attrs: nofree nosync nounwind readnone speculatable willreturn
declare x86_fp80 @llvm.fabs.f80(x86_fp80) #2

; Function Attrs: argmemonly nofree nounwind readonly willreturn
declare i32 @bcmp(i8* nocapture, i8* nocapture, i64) local_unnamed_addr #3

; Function Attrs: noreturn nounwind
declare dso_local void @abort() local_unnamed_addr #4

; Function Attrs: argmemonly nofree nosync nounwind willreturn
declare void @llvm.lifetime.end.p0i8(i64 immarg, i8* nocapture) #1

; Function Attrs: nounwind uwtable
define dso_local i32 @main() local_unnamed_addr #0 {
  tail call void @testl()
  ret i32 0
}

attributes #0 = { nounwind uwtable "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { argmemonly nofree nosync nounwind willreturn }
attributes #2 = { nofree nosync nounwind readnone speculatable willreturn }
attributes #3 = { argmemonly nofree nounwind readonly willreturn }
attributes #4 = { noreturn nounwind "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #5 = { nounwind }
attributes #6 = { noreturn nounwind }

!llvm.ident = !{!0}
!llvm.module.flags = !{!1, !2, !3, !5}

!0 = !{!"clang version 12.0.0"}
!1 = !{i32 1, !"wchar_size", i32 4}
!2 = !{i32 2, !"bcdb.elf.type", i32 2}
!3 = !{i32 6, !"bcdb.elf.runpath", !4}
!4 = !{!"/nix/store/wfz1ijnmi8xrpw9bwivqih9szbbprfah-shell/lib64", !"/nix/store/wfz1ijnmi8xrpw9bwivqih9szbbprfah-shell/lib", !"/nix/store/sbbifs2ykc05inws26203h0xwcadnf0l-glibc-2.32-46/lib"}
!5 = !{i32 6, !"bcdb.elf.needed", !6}
!6 = !{!"libc.so.6"}
!7 = !{!8, !8, i64 0}
!8 = !{!"long double", !9, i64 0}
!9 = !{!"omnipotent char", !10, i64 0}
!10 = !{!"Simple C/C++ TBAA"}

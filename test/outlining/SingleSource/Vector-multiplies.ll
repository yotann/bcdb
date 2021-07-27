; RUN: %outliningtest %s

source_filename = "build-llvm12/SingleSource/UnitTests/Vector/Vector-multiplies"


@TheArray = dso_local local_unnamed_addr global [100000 x double] zeroinitializer, align 16
@.str = private unnamed_addr constant [13 x i8] c"%u %u %u %u\0A\00", align 1

; Function Attrs: nofree nounwind uwtable
define dso_local i32 @main(i32 %0, i8** nocapture readnone %1) local_unnamed_addr #0 {
  br label %3

3:                                                ; preds = %3, %2
  %4 = phi i64 [ 0, %2 ], [ %29, %3 ]
  %5 = trunc i64 %4 to i32
  %6 = uitofp i32 %5 to double
  %7 = fmul double %6, 1.234500e+01
  %8 = getelementptr inbounds [100000 x double], [100000 x double]* @TheArray, i64 0, i64 %4
  store double %7, double* %8, align 8, !tbaa !7
  %9 = add nuw nsw i64 %4, 1
  %10 = trunc i64 %9 to i32
  %11 = uitofp i32 %10 to double
  %12 = fmul double %11, 1.234500e+01
  %13 = getelementptr inbounds [100000 x double], [100000 x double]* @TheArray, i64 0, i64 %9
  store double %12, double* %13, align 8, !tbaa !7
  %14 = add nuw nsw i64 %4, 2
  %15 = trunc i64 %14 to i32
  %16 = uitofp i32 %15 to double
  %17 = fmul double %16, 1.234500e+01
  %18 = getelementptr inbounds [100000 x double], [100000 x double]* @TheArray, i64 0, i64 %14
  store double %17, double* %18, align 8, !tbaa !7
  %19 = add nuw nsw i64 %4, 3
  %20 = trunc i64 %19 to i32
  %21 = uitofp i32 %20 to double
  %22 = fmul double %21, 1.234500e+01
  %23 = getelementptr inbounds [100000 x double], [100000 x double]* @TheArray, i64 0, i64 %19
  store double %22, double* %23, align 8, !tbaa !7
  %24 = add nuw nsw i64 %4, 4
  %25 = trunc i64 %24 to i32
  %26 = uitofp i32 %25 to double
  %27 = fmul double %26, 1.234500e+01
  %28 = getelementptr inbounds [100000 x double], [100000 x double]* @TheArray, i64 0, i64 %24
  store double %27, double* %28, align 8, !tbaa !7
  %29 = add nuw nsw i64 %4, 5
  %30 = icmp eq i64 %29, 100000
  br i1 %30, label %31, label %3, !llvm.loop !11

31:                                               ; preds = %3
  %32 = tail call i32 (i8*, ...) @printf(i8* nonnull dereferenceable(1) getelementptr inbounds ([13 x i8], [13 x i8]* @.str, i64 0, i64 0), i32 0, i32 0, i32 0, i32 0) #2
  %33 = tail call i32 (i8*, ...) @printf(i8* nonnull dereferenceable(1) getelementptr inbounds ([13 x i8], [13 x i8]* @.str, i64 0, i64 0), i32 0, i32 0, i32 0, i32 0) #2
  %34 = tail call i32 (i8*, ...) @printf(i8* nonnull dereferenceable(1) getelementptr inbounds ([13 x i8], [13 x i8]* @.str, i64 0, i64 0), i32 0, i32 0, i32 0, i32 0) #2
  %35 = tail call i32 (i8*, ...) @printf(i8* nonnull dereferenceable(1) getelementptr inbounds ([13 x i8], [13 x i8]* @.str, i64 0, i64 0), i32 0, i32 0, i32 0, i32 0) #2
  ret i32 0
}

; Function Attrs: nofree nounwind
declare dso_local noundef i32 @printf(i8* nocapture noundef readonly, ...) local_unnamed_addr #1

attributes #0 = { nofree nounwind uwtable "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { nofree nounwind "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #2 = { nounwind }

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
!8 = !{!"double", !9, i64 0}
!9 = !{!"omnipotent char", !10, i64 0}
!10 = !{!"Simple C/C++ TBAA"}
!11 = distinct !{!11, !12}
!12 = !{!"llvm.loop.mustprogress"}

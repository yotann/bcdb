; RUN: %outliningtest --no-run %s

source_filename = "build-llvm12/SingleSource/Benchmarks/Misc/revertBits"

@.str.2 = private unnamed_addr constant [14 x i8] c"0x%x -> 0x%x\0A\00", align 1
@.str.3 = private unnamed_addr constant [18 x i8] c"0x%llx -> 0x%llx\0A\00", align 1

; Function Attrs: noinline norecurse nounwind readnone uwtable willreturn
define dso_local i32 @ReverseBits32(i32 %0) local_unnamed_addr #0 {
  %2 = lshr i32 %0, 1
  %3 = and i32 %2, 1431655765
  %4 = shl i32 %0, 1
  %5 = and i32 %4, -1431655766
  %6 = or i32 %3, %5
  %7 = lshr i32 %6, 2
  %8 = and i32 %7, 858993459
  %9 = shl i32 %6, 2
  %10 = and i32 %9, -858993460
  %11 = or i32 %8, %10
  %12 = lshr i32 %11, 4
  %13 = and i32 %12, 252645135
  %14 = shl i32 %11, 4
  %15 = and i32 %14, -252645136
  %16 = or i32 %13, %15
  %17 = lshr i32 %16, 24
  %18 = lshr i32 %16, 8
  %19 = and i32 %18, 65280
  %20 = shl i32 %16, 8
  %21 = and i32 %20, 16711680
  %22 = shl i32 %16, 24
  %23 = or i32 %22, %17
  %24 = or i32 %23, %19
  %25 = or i32 %24, %21
  ret i32 %25
}

; Function Attrs: noinline norecurse nounwind readnone uwtable willreturn
define dso_local i64 @ReverseBits64(i64 %0) local_unnamed_addr #0 {
  %2 = lshr i64 %0, 1
  %3 = and i64 %2, 6148914691236517205
  %4 = shl i64 %0, 1
  %5 = and i64 %4, -6148914691236517206
  %6 = or i64 %3, %5
  %7 = lshr i64 %6, 2
  %8 = and i64 %7, 3689348814741910323
  %9 = shl i64 %6, 2
  %10 = and i64 %9, -3689348814741910324
  %11 = or i64 %8, %10
  %12 = lshr i64 %11, 4
  %13 = and i64 %12, 1085102592571150095
  %14 = shl i64 %11, 4
  %15 = and i64 %14, -1085102592571150096
  %16 = or i64 %13, %15
  %17 = lshr i64 %16, 56
  %18 = lshr i64 %16, 40
  %19 = and i64 %18, 65280
  %20 = lshr i64 %16, 24
  %21 = and i64 %20, 16711680
  %22 = lshr i64 %16, 8
  %23 = and i64 %22, 4278190080
  %24 = shl i64 %16, 56
  %25 = shl i64 %16, 40
  %26 = and i64 %25, 71776119061217280
  %27 = shl i64 %16, 24
  %28 = and i64 %27, 280375465082880
  %29 = shl i64 %16, 8
  %30 = and i64 %29, 1095216660480
  %31 = or i64 %24, %17
  %32 = or i64 %31, %19
  %33 = or i64 %32, %21
  %34 = or i64 %33, %23
  %35 = or i64 %34, %26
  %36 = or i64 %35, %28
  %37 = or i64 %36, %30
  ret i64 %37
}

; Function Attrs: nofree nounwind uwtable
define dso_local i32 @main() local_unnamed_addr #1 {
  br label %1

1:                                                ; preds = %1, %0
  %2 = phi i64 [ 0, %0 ], [ %11, %1 ]
  %3 = phi i64 [ 0, %0 ], [ %8, %1 ]
  %4 = phi i64 [ 0, %0 ], [ %10, %1 ]
  %5 = trunc i64 %2 to i32
  %6 = tail call i32 @ReverseBits32(i32 %5)
  %7 = zext i32 %6 to i64
  %8 = add i64 %3, %7
  %9 = tail call i64 @ReverseBits64(i64 %2)
  %10 = add i64 %9, %4
  %11 = add nuw nsw i64 %2, 1
  %12 = icmp eq i64 %11, 16777216
  br i1 %12, label %19, label %1, !llvm.loop !7

13:                                               ; preds = %19
  %14 = tail call i32 (i8*, ...) @printf(i8* nonnull dereferenceable(1) getelementptr inbounds ([14 x i8], [14 x i8]* @.str.2, i64 0, i64 0), i32 305419896, i32 510274632)
  %15 = tail call i32 (i8*, ...) @printf(i8* nonnull dereferenceable(1) getelementptr inbounds ([18 x i8], [18 x i8]* @.str.3, i64 0, i64 0), i64 81985529205302085, i64 -6718103380001897344)
  %16 = or i64 %26, %28
  %17 = icmp ne i64 %16, 0
  %18 = zext i1 %17 to i32
  ret i32 %18

19:                                               ; preds = %19, %1
  %20 = phi i64 [ %29, %19 ], [ 0, %1 ]
  %21 = phi i64 [ %26, %19 ], [ %8, %1 ]
  %22 = phi i64 [ %28, %19 ], [ %10, %1 ]
  %23 = trunc i64 %20 to i32
  %24 = tail call i32 @llvm.bitreverse.i32(i32 %23)
  %25 = zext i32 %24 to i64
  %26 = sub i64 %21, %25
  %27 = tail call i64 @llvm.bitreverse.i64(i64 %20)
  %28 = sub i64 %22, %27
  %29 = add nuw nsw i64 %20, 1
  %30 = icmp eq i64 %29, 16777216
  br i1 %30, label %13, label %19, !llvm.loop !9
}

; Function Attrs: nofree nounwind
declare dso_local i32 @printf(i8* nocapture readonly, ...) local_unnamed_addr #2

; Function Attrs: nofree nosync nounwind readnone speculatable willreturn
declare i32 @llvm.bitreverse.i32(i32) #3

; Function Attrs: nofree nosync nounwind readnone speculatable willreturn
declare i64 @llvm.bitreverse.i64(i64) #3

attributes #0 = { noinline norecurse nounwind readnone uwtable willreturn "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { nofree nounwind uwtable "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #2 = { nofree nounwind "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #3 = { nofree nosync nounwind readnone speculatable willreturn }

!llvm.ident = !{!0}
!llvm.module.flags = !{!1, !2, !3, !5}

!0 = !{!"clang version 12.0.0"}
!1 = !{i32 1, !"wchar_size", i32 4}
!2 = !{i32 2, !"bcdb.elf.type", i32 2}
!3 = !{i32 6, !"bcdb.elf.runpath", !4}
!4 = !{!"/nix/store/wfz1ijnmi8xrpw9bwivqih9szbbprfah-shell/lib64", !"/nix/store/wfz1ijnmi8xrpw9bwivqih9szbbprfah-shell/lib", !"/nix/store/sbbifs2ykc05inws26203h0xwcadnf0l-glibc-2.32-46/lib"}
!5 = !{i32 6, !"bcdb.elf.needed", !6}
!6 = !{!"libm.so.6", !"libc.so.6"}
!7 = distinct !{!7, !8}
!8 = !{!"llvm.loop.mustprogress"}
!9 = distinct !{!9, !8}

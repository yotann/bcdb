; RUN: %outliningtest %s

source_filename = "build-llvm12/SingleSource/Regression/C/gcc-c-torture/execute/GCC-C-execute-921117-1"

%struct.s = type { [11 x i8], i32 }

@cell = dso_local local_unnamed_addr global %struct.s zeroinitializer, align 8
@.str = private unnamed_addr constant [11 x i8] c"0123456789\00", align 1

; Function Attrs: nounwind readnone uwtable willreturn
define dso_local i32 @check(i64 %0, i64 %1) local_unnamed_addr #0 {
  %3 = alloca { i64, i64 }, align 8
  %4 = getelementptr inbounds { i64, i64 }, { i64, i64 }* %3, i64 0, i32 0
  store i64 %0, i64* %4, align 8
  %5 = getelementptr inbounds { i64, i64 }, { i64, i64 }* %3, i64 0, i32 1
  store i64 %1, i64* %5, align 8
  %6 = and i64 %1, -4294967296
  %7 = icmp eq i64 %6, 425201762304
  br i1 %7, label %8, label %11

8:                                                ; preds = %2
  %9 = bitcast { i64, i64 }* %3 to i8*
  %10 = call i32 @strcmp(i8* nonnull %9, i8* nonnull dereferenceable(11) getelementptr inbounds ([11 x i8], [11 x i8]* @.str, i64 0, i64 0))
  br label %11

11:                                               ; preds = %8, %2
  %12 = phi i32 [ %10, %8 ], [ 1, %2 ]
  ret i32 %12
}

; Function Attrs: argmemonly nofree nounwind readonly willreturn
declare dso_local i32 @strcmp(i8* nocapture, i8* nocapture) local_unnamed_addr #1

; Function Attrs: noreturn nounwind uwtable
define dso_local i32 @main() local_unnamed_addr #2 {
  %1 = alloca { i64, i64 }, align 8
  store i32 99, i32* getelementptr inbounds (%struct.s, %struct.s* @cell, i64 0, i32 1), align 4, !tbaa !7
  tail call void @llvm.memcpy.p0i8.p0i8.i64(i8* nonnull align 8 dereferenceable(11) getelementptr inbounds (%struct.s, %struct.s* @cell, i64 0, i32 0, i64 0), i8* nonnull align 1 dereferenceable(11) getelementptr inbounds ([11 x i8], [11 x i8]* @.str, i64 0, i64 0), i64 11, i1 false)
  %2 = load i64, i64* bitcast (i8* getelementptr inbounds (%struct.s, %struct.s* @cell, i64 0, i32 0, i64 8) to i64*), align 8
  %3 = bitcast { i64, i64 }* %1 to i8*
  call void @llvm.lifetime.start.p0i8(i64 16, i8* nonnull %3)
  %4 = getelementptr inbounds { i64, i64 }, { i64, i64 }* %1, i64 0, i32 0
  store i64 3978425819141910832, i64* %4, align 8
  %5 = getelementptr inbounds { i64, i64 }, { i64, i64 }* %1, i64 0, i32 1
  store i64 %2, i64* %5, align 8
  %6 = and i64 %2, -4294967296
  %7 = icmp eq i64 %6, 425201762304
  br i1 %7, label %9, label %8

8:                                                ; preds = %0
  call void @llvm.lifetime.end.p0i8(i64 16, i8* nonnull %3)
  br label %12

9:                                                ; preds = %0
  %10 = call i32 @bcmp(i8* nonnull dereferenceable(11) %3, i8* nonnull dereferenceable(11) getelementptr inbounds ([11 x i8], [11 x i8]* @.str, i64 0, i64 0), i64 11)
  call void @llvm.lifetime.end.p0i8(i64 16, i8* nonnull %3)
  %11 = icmp eq i32 %10, 0
  br i1 %11, label %13, label %12

12:                                               ; preds = %9, %8
  tail call void @abort() #6
  unreachable

13:                                               ; preds = %9
  tail call void @exit(i32 0) #6
  unreachable
}

; Function Attrs: argmemonly nofree nosync nounwind willreturn
declare void @llvm.memcpy.p0i8.p0i8.i64(i8* noalias nocapture writeonly, i8* noalias nocapture readonly, i64, i1 immarg) #3

; Function Attrs: argmemonly nofree nosync nounwind willreturn
declare void @llvm.lifetime.start.p0i8(i64 immarg, i8* nocapture) #3

; Function Attrs: argmemonly nofree nosync nounwind willreturn
declare void @llvm.lifetime.end.p0i8(i64 immarg, i8* nocapture) #3

; Function Attrs: argmemonly nofree nounwind readonly willreturn
declare i32 @bcmp(i8* nocapture, i8* nocapture, i64) local_unnamed_addr #4

; Function Attrs: noreturn
declare dso_local void @abort() local_unnamed_addr #5

; Function Attrs: noreturn
declare dso_local void @exit(i32) local_unnamed_addr #5

attributes #0 = { nounwind readnone uwtable willreturn "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { argmemonly nofree nounwind readonly willreturn "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #2 = { noreturn nounwind uwtable "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #3 = { argmemonly nofree nosync nounwind willreturn }
attributes #4 = { argmemonly nofree nounwind readonly willreturn }
attributes #5 = { noreturn "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "unsafe-fp-math"="false" "use-soft-float"="false" }
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
!7 = !{!8, !11, i64 12}
!8 = !{!"s", !9, i64 0, !11, i64 12}
!9 = !{!"omnipotent char", !10, i64 0}
!10 = !{!"Simple C/C++ TBAA"}
!11 = !{!"int", !9, i64 0}

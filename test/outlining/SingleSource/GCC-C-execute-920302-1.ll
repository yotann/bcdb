; RUN: %outliningtest %s

source_filename = "build-llvm12/SingleSource/Regression/C/gcc-c-torture/execute/GCC-C-execute-920302-1"

@buf = dso_local global [10 x i8] zeroinitializer, align 1
@optab = dso_local local_unnamed_addr global [5 x i16] zeroinitializer, align 2
@p = dso_local global [5 x i16] zeroinitializer, align 2
@.str = private unnamed_addr constant [6 x i8] c"xyxyz\00", align 1

; Function Attrs: noreturn nounwind uwtable
define dso_local i32 @main() local_unnamed_addr #1 {
  store i16 sub (i16 ptrtoint (i8* blockaddress(@execute, %4) to i16), i16 ptrtoint (i8* blockaddress(@execute, %4) to i16)), i16* getelementptr inbounds ([5 x i16], [5 x i16]* @optab, i64 0, i64 0), align 2, !tbaa !7
  store i16 sub (i16 ptrtoint (i8* blockaddress(@execute, %7) to i16), i16 ptrtoint (i8* blockaddress(@execute, %4) to i16)), i16* getelementptr inbounds ([5 x i16], [5 x i16]* @optab, i64 0, i64 1), align 2, !tbaa !7
  store i16 sub (i16 ptrtoint (i8* blockaddress(@execute, %8) to i16), i16 ptrtoint (i8* blockaddress(@execute, %4) to i16)), i16* getelementptr inbounds ([5 x i16], [5 x i16]* @optab, i64 0, i64 2), align 2, !tbaa !7
  store i16 sub (i16 ptrtoint (i8* blockaddress(@execute, %7) to i16), i16 ptrtoint (i8* blockaddress(@execute, %4) to i16)), i16* getelementptr inbounds ([5 x i16], [5 x i16]* @p, i64 0, i64 0), align 2, !tbaa !7
  store i16 sub (i16 ptrtoint (i8* blockaddress(@execute, %4) to i16), i16 ptrtoint (i8* blockaddress(@execute, %4) to i16)), i16* getelementptr inbounds ([5 x i16], [5 x i16]* @p, i64 0, i64 1), align 2, !tbaa !7
  store i16 sub (i16 ptrtoint (i8* blockaddress(@execute, %7) to i16), i16 ptrtoint (i8* blockaddress(@execute, %4) to i16)), i16* getelementptr inbounds ([5 x i16], [5 x i16]* @p, i64 0, i64 2), align 2, !tbaa !7
  store i16 sub (i16 ptrtoint (i8* blockaddress(@execute, %8) to i16), i16 ptrtoint (i8* blockaddress(@execute, %4) to i16)), i16* getelementptr inbounds ([5 x i16], [5 x i16]* @p, i64 0, i64 3), align 2, !tbaa !7
  %1 = tail call i32 @execute(i16* getelementptr inbounds ([5 x i16], [5 x i16]* @p, i64 0, i64 0))
  %2 = tail call i32 @bcmp(i8* nonnull dereferenceable(6) getelementptr inbounds ([10 x i8], [10 x i8]* @buf, i64 0, i64 0), i8* nonnull dereferenceable(6) getelementptr inbounds ([6 x i8], [6 x i8]* @.str, i64 0, i64 0), i64 6)
  %3 = icmp eq i32 %2, 0
  br i1 %3, label %5, label %4

4:                                                ; preds = %0
  tail call void @abort() #4
  unreachable

5:                                                ; preds = %0
  tail call void @exit(i32 0) #4
  unreachable
}

; Function Attrs: nofree norecurse nounwind uwtable
define dso_local i32 @execute(i16* readonly %0) #0 {
  %2 = icmp eq i16* %0, null
  br i1 %2, label %3, label %4

3:                                                ; preds = %1
  store i16 sub (i16 ptrtoint (i8* blockaddress(@execute, %4) to i16), i16 ptrtoint (i8* blockaddress(@execute, %4) to i16)), i16* getelementptr inbounds ([5 x i16], [5 x i16]* @optab, i64 0, i64 0), align 2, !tbaa !7
  store i16 sub (i16 ptrtoint (i8* blockaddress(@execute, %7) to i16), i16 ptrtoint (i8* blockaddress(@execute, %4) to i16)), i16* getelementptr inbounds ([5 x i16], [5 x i16]* @optab, i64 0, i64 1), align 2, !tbaa !7
  store i16 sub (i16 ptrtoint (i8* blockaddress(@execute, %8) to i16), i16 ptrtoint (i8* blockaddress(@execute, %4) to i16)), i16* getelementptr inbounds ([5 x i16], [5 x i16]* @optab, i64 0, i64 2), align 2, !tbaa !7
  br label %10

4:                                                ; preds = %11, %1
  %5 = phi i16* [ %16, %11 ], [ %0, %1 ]
  %6 = phi i8* [ %17, %11 ], [ getelementptr inbounds ([10 x i8], [10 x i8]* @buf, i64 0, i64 0), %1 ]
  store i8 120, i8* %6, align 1, !tbaa !11
  br label %11

7:                                                ; preds = %11
  store i8 121, i8* %17, align 1, !tbaa !11
  br label %11

8:                                                ; preds = %11
  %9 = getelementptr inbounds i8, i8* %13, i64 2
  store i8 122, i8* %17, align 1, !tbaa !11
  store i8 0, i8* %9, align 1, !tbaa !11
  br label %10

10:                                               ; preds = %8, %3
  ret i32 undef

11:                                               ; preds = %7, %4
  %12 = phi i16* [ %5, %4 ], [ %16, %7 ]
  %13 = phi i8* [ %6, %4 ], [ %17, %7 ]
  %14 = load i16, i16* %12, align 2, !tbaa !7
  %15 = sext i16 %14 to i64
  %16 = getelementptr inbounds i16, i16* %12, i64 1
  %17 = getelementptr inbounds i8, i8* %13, i64 1
  %18 = getelementptr i8, i8* blockaddress(@execute, %4), i64 %15
  indirectbr i8* %18, [label %4, label %8, label %7]
}

; Function Attrs: argmemonly nofree nounwind readonly willreturn
declare i32 @bcmp(i8* nocapture, i8* nocapture, i64) local_unnamed_addr #2

; Function Attrs: noreturn
declare dso_local void @abort() local_unnamed_addr #3

; Function Attrs: noreturn
declare dso_local void @exit(i32) local_unnamed_addr #3

attributes #0 = { nofree norecurse nounwind uwtable "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { noreturn nounwind uwtable "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #2 = { argmemonly nofree nounwind readonly willreturn }
attributes #3 = { noreturn "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #4 = { noreturn nounwind }

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
!8 = !{!"short", !9, i64 0}
!9 = !{!"omnipotent char", !10, i64 0}
!10 = !{!"Simple C/C++ TBAA"}
!11 = !{!9, !9, i64 0}

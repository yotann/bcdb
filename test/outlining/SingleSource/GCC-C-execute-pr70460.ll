; RUN: %outliningtest %s

source_filename = "build-llvm12/SingleSource/Regression/C/gcc-c-torture/execute/GCC-C-execute-pr70460"

@c = dso_local local_unnamed_addr global i32 0, align 4
@foo.b = internal unnamed_addr constant [2 x i32] [i32 trunc (i64 sub (i64 ptrtoint (i8* blockaddress(@foo, %7) to i64), i64 ptrtoint (i8* blockaddress(@foo, %13) to i64)) to i32), i32 trunc (i64 sub (i64 ptrtoint (i8* blockaddress(@foo, %10) to i64), i64 ptrtoint (i8* blockaddress(@foo, %13) to i64)) to i32)], align 4

; Function Attrs: nofree noinline norecurse nounwind uwtable willreturn
define dso_local void @foo(i32 %0) #0 {
  %2 = sext i32 %0 to i64
  %3 = getelementptr inbounds [2 x i32], [2 x i32]* @foo.b, i64 0, i64 %2
  %4 = load i32, i32* %3, align 4, !tbaa !7
  %5 = sext i32 %4 to i64
  %6 = getelementptr i8, i8* blockaddress(@foo, %13), i64 %5
  indirectbr i8* %6, [label %7, label %13, label %10]

7:                                                ; preds = %1
  %8 = load i32, i32* @c, align 4, !tbaa !7
  %9 = add nsw i32 %8, 2
  store i32 %9, i32* @c, align 4, !tbaa !7
  br label %10

10:                                               ; preds = %7, %1
  %11 = load i32, i32* @c, align 4, !tbaa !7
  %12 = add nsw i32 %11, 1
  store i32 %12, i32* @c, align 4, !tbaa !7
  br label %13

13:                                               ; preds = %10, %1
  ret void
}

; Function Attrs: nounwind uwtable
define dso_local i32 @main() local_unnamed_addr #1 {
  tail call void @foo(i32 0)
  %1 = load i32, i32* @c, align 4, !tbaa !7
  %2 = icmp eq i32 %1, 3
  br i1 %2, label %4, label %3

3:                                                ; preds = %0
  tail call void @abort() #3
  unreachable

4:                                                ; preds = %0
  tail call void @foo(i32 1)
  %5 = load i32, i32* @c, align 4, !tbaa !7
  %6 = icmp eq i32 %5, 4
  br i1 %6, label %8, label %7

7:                                                ; preds = %4
  tail call void @abort() #3
  unreachable

8:                                                ; preds = %4
  ret i32 0
}

; Function Attrs: noreturn nounwind
declare dso_local void @abort() local_unnamed_addr #2

attributes #0 = { nofree noinline norecurse nounwind uwtable willreturn "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { nounwind uwtable "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #2 = { noreturn nounwind "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #3 = { noreturn nounwind }

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
!8 = !{!"int", !9, i64 0}
!9 = !{!"omnipotent char", !10, i64 0}
!10 = !{!"Simple C/C++ TBAA"}

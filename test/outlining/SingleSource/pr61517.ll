; RUN: %outliningtest %s

source_filename = "build-llvm12/SingleSource/Regression/C/gcc-c-torture/execute/GCC-C-execute-pr61517"

@a = dso_local global i32 0, align 4
@c = dso_local local_unnamed_addr global i32* @a, align 8
@b = dso_local local_unnamed_addr global i32 0, align 4
@d = dso_local local_unnamed_addr global i16 0, align 2

; Function Attrs: nounwind uwtable
define dso_local i32 @main() local_unnamed_addr #0 {
  %1 = load i32, i32* @a, align 4, !tbaa !7
  %2 = load i32*, i32** @c, align 8, !tbaa !11
  store i32 1, i32* %2, align 4, !tbaa !7
  %3 = load i32, i32* @b, align 4, !tbaa !7
  %4 = icmp eq i32 %3, 0
  br i1 %4, label %5, label %7

5:                                                ; preds = %0
  %6 = trunc i32 %1 to i16
  store i16 %6, i16* @d, align 2, !tbaa !13
  store i32 %1, i32* %2, align 4, !tbaa !7
  br label %7

7:                                                ; preds = %5, %0
  %8 = load i32, i32* @a, align 4, !tbaa !7
  %9 = icmp eq i32 %8, 0
  br i1 %9, label %11, label %10

10:                                               ; preds = %7
  tail call void @abort() #2
  unreachable

11:                                               ; preds = %7
  ret i32 0
}

; Function Attrs: noreturn nounwind
declare dso_local void @abort() local_unnamed_addr #1

attributes #0 = { nounwind uwtable "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { noreturn nounwind "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #2 = { noreturn nounwind }

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
!11 = !{!12, !12, i64 0}
!12 = !{!"any pointer", !9, i64 0}
!13 = !{!14, !14, i64 0}
!14 = !{!"short", !9, i64 0}

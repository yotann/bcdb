; RUN: %outliningtest --no-run %s

source_filename = "build-llvm12/SingleSource/Regression/C/gcc-c-torture/execute/GCC-C-execute-pr33669"

%struct.foo_t = type { i32, i32 }

; Function Attrs: nofree norecurse nounwind uwtable willreturn
define dso_local i64 @foo(%struct.foo_t* nocapture %0, i64 %1, i32 %2) local_unnamed_addr #0 {
  %4 = getelementptr inbounds %struct.foo_t, %struct.foo_t* %0, i64 0, i32 0
  %5 = load i32, i32* %4, align 4, !tbaa !7
  %6 = zext i32 %5 to i64
  %7 = srem i64 %1, %6
  %8 = sub nsw i64 %1, %7
  %9 = trunc i64 %7 to i32
  %10 = add i32 %2, -1
  %11 = add i32 %10, %5
  %12 = add i32 %11, %9
  %13 = urem i32 %12, %5
  %14 = sub i32 %12, %13
  %15 = icmp ult i32 %5, %14
  br i1 %15, label %21, label %16

16:                                               ; preds = %3
  %17 = getelementptr inbounds %struct.foo_t, %struct.foo_t* %0, i64 0, i32 1
  %18 = load i32, i32* %17, align 4, !tbaa !12
  %19 = icmp ugt i32 %18, %5
  br i1 %19, label %20, label %21

20:                                               ; preds = %16
  store i32 %5, i32* %17, align 4, !tbaa !12
  br label %21

21:                                               ; preds = %20, %16, %3
  %22 = phi i64 [ -1, %3 ], [ %8, %20 ], [ %8, %16 ]
  ret i64 %22
}

; Function Attrs: nounwind uwtable
define dso_local i32 @main() local_unnamed_addr #1 {
  ret i32 0
}

attributes #0 = { nofree norecurse nounwind uwtable willreturn "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { nounwind uwtable "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "unsafe-fp-math"="false" "use-soft-float"="false" }

!llvm.ident = !{!0}
!llvm.module.flags = !{!1, !2, !3, !5}

!0 = !{!"clang version 12.0.0"}
!1 = !{i32 1, !"wchar_size", i32 4}
!2 = !{i32 2, !"bcdb.elf.type", i32 2}
!3 = !{i32 6, !"bcdb.elf.runpath", !4}
!4 = !{!"/nix/store/wfz1ijnmi8xrpw9bwivqih9szbbprfah-shell/lib64", !"/nix/store/wfz1ijnmi8xrpw9bwivqih9szbbprfah-shell/lib", !"/nix/store/sbbifs2ykc05inws26203h0xwcadnf0l-glibc-2.32-46/lib"}
!5 = !{i32 6, !"bcdb.elf.needed", !6}
!6 = !{!"libc.so.6"}
!7 = !{!8, !9, i64 0}
!8 = !{!"foo_t", !9, i64 0, !9, i64 4}
!9 = !{!"int", !10, i64 0}
!10 = !{!"omnipotent char", !11, i64 0}
!11 = !{!"Simple C/C++ TBAA"}
!12 = !{!8, !9, i64 4}

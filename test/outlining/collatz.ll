; RUN: %outliningtest --no-run %s

source_filename = "collatz.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; Function Attrs: norecurse nounwind readnone uwtable
define dso_local i32 @collatz_len(i32) local_unnamed_addr #0 {
  %2 = icmp sgt i32 %0, 1
  br i1 %2, label %3, label %14

3:                                                ; preds = %3, %1
  %4 = phi i32 [ %12, %3 ], [ 0, %1 ]
  %5 = phi i32 [ %11, %3 ], [ %0, %1 ]
  %6 = and i32 %5, 1
  %7 = icmp eq i32 %6, 0
  %8 = mul nsw i32 %5, 3
  %9 = add nsw i32 %8, 1
  %10 = lshr i32 %5, 1
  %11 = select i1 %7, i32 %10, i32 %9
  %12 = add nuw nsw i32 %4, 1
  %13 = icmp sgt i32 %11, 1
  br i1 %13, label %3, label %14

14:                                               ; preds = %3, %1
  %15 = phi i32 [ 0, %1 ], [ %12, %3 ]
  ret i32 %15
}

attributes #0 = { norecurse nounwind readnone uwtable "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "unsafe-fp-math"="false" "use-soft-float"="false" }

!llvm.module.flags = !{!0, !1, !2}
!llvm.ident = !{!3}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"ThinLTO", i32 0}
!2 = !{i32 1, !"EnableSplitLTOUnit", i32 1}
!3 = !{!"clang version 11.0.1"}

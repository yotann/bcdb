; RUN: %outliningtest --no-run %s

source_filename = "build-llvm12/SingleSource/Regression/C/gcc-c-torture/execute/GCC-C-execute-pr31169"

%struct.tree_type = type { i16, [2 x i8] }

; Function Attrs: norecurse nounwind readonly uwtable willreturn
define dso_local i32 @sign_bit_p(%struct.tree_type* nocapture readonly %0, i64 %1, i64 %2) local_unnamed_addr #0 {
  %4 = getelementptr %struct.tree_type, %struct.tree_type* %0, i64 0, i32 0
  %5 = load i16, i16* %4, align 4
  %6 = and i16 %5, 511
  %7 = zext i16 %6 to i64
  %8 = icmp ugt i16 %6, 64
  br i1 %8, label %9, label %14

9:                                                ; preds = %3
  %10 = add nsw i64 %7, -65
  %11 = shl nuw i64 1, %10
  %12 = sub nsw i64 128, %7
  %13 = lshr i64 -1, %12
  br label %20

14:                                               ; preds = %3
  %15 = add nuw nsw i64 %7, 4294967295
  %16 = and i64 %15, 4294967295
  %17 = shl nuw i64 1, %16
  %18 = sub nsw i64 64, %7
  %19 = lshr i64 -1, %18
  br label %20

20:                                               ; preds = %14, %9
  %21 = phi i64 [ -1, %9 ], [ %19, %14 ]
  %22 = phi i64 [ 0, %9 ], [ %17, %14 ]
  %23 = phi i64 [ %13, %9 ], [ 0, %14 ]
  %24 = phi i64 [ %11, %9 ], [ 0, %14 ]
  %25 = and i64 %23, %1
  %26 = icmp eq i64 %25, %24
  %27 = and i64 %21, %2
  %28 = icmp eq i64 %27, %22
  %29 = and i1 %28, %26
  %30 = zext i1 %29 to i32
  ret i32 %30
}

; Function Attrs: nounwind uwtable
define dso_local i32 @main() local_unnamed_addr #1 {
  ret i32 0
}

attributes #0 = { norecurse nounwind readonly uwtable willreturn "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { nounwind uwtable "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "unsafe-fp-math"="false" "use-soft-float"="false" }

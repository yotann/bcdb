; RUN: %outliningtest %s

source_filename = "build-llvm12/SingleSource/Regression/C/gcc-c-torture/execute/GCC-C-execute-990222-1"

@line = dso_local local_unnamed_addr global [4 x i8] c"199\00", align 1

; Function Attrs: nounwind uwtable
define dso_local i32 @main() local_unnamed_addr #0 {
  %1 = load i8, i8* getelementptr inbounds ([4 x i8], [4 x i8]* @line, i64 0, i64 2), align 1
  %2 = add i8 %1, 1
  store i8 %2, i8* getelementptr inbounds ([4 x i8], [4 x i8]* @line, i64 0, i64 2), align 1
  %3 = icmp sgt i8 %2, 57
  br i1 %3, label %4, label %12

4:                                                ; preds = %4, %0
  %5 = phi i8* [ %6, %4 ], [ getelementptr inbounds ([4 x i8], [4 x i8]* @line, i64 0, i64 2), %0 ]
  store i8 48, i8* %5, align 1
  %6 = getelementptr inbounds i8, i8* %5, i64 -1
  %7 = load i8, i8* %6, align 1
  %8 = add i8 %7, 1
  store i8 %8, i8* %6, align 1
  %9 = icmp sgt i8 %8, 57
  br i1 %9, label %4, label %10

10:                                               ; preds = %4
  %11 = load i8, i8* getelementptr inbounds ([4 x i8], [4 x i8]* @line, i64 0, i64 2), align 1
  br label %12

12:                                               ; preds = %10, %0
  %13 = phi i8 [ %11, %10 ], [ %2, %0 ]
  %14 = load i8, i8* getelementptr inbounds ([4 x i8], [4 x i8]* @line, i64 0, i64 0), align 1
  %15 = icmp eq i8 %14, 50
  %16 = load i8, i8* getelementptr inbounds ([4 x i8], [4 x i8]* @line, i64 0, i64 1), align 1
  %17 = icmp eq i8 %16, 48
  %18 = and i1 %15, %17
  %19 = icmp eq i8 %13, 48
  %20 = and i1 %18, %19
  br i1 %20, label %22, label %21

21:                                               ; preds = %12
  tail call void @abort() #2
  unreachable

22:                                               ; preds = %12
  ret i32 0
}

; Function Attrs: noreturn
declare dso_local void @abort() local_unnamed_addr #1

attributes #0 = { nounwind uwtable "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { noreturn "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #2 = { noreturn nounwind }

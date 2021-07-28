; RUN: %outliningtest %s

source_filename = "build-llvm12/SingleSource/UnitTests/2003-10-13-SwitchTest"

@str = private unnamed_addr constant [4 x i8] c"BAD\00", align 1
@str.2 = private unnamed_addr constant [5 x i8] c"GOOD\00", align 1

; Function Attrs: nofree nounwind uwtable
define dso_local i32 @main(i32 %0, i8** nocapture readnone %1) local_unnamed_addr #0 {
  switch i32 %0, label %4 [
    i32 100, label %3
    i32 101, label %3
    i32 1023, label %3
  ]

3:                                                ; preds = %2, %2, %2
  br label %4

4:                                                ; preds = %3, %2
  %5 = phi i8* [ getelementptr inbounds ([4 x i8], [4 x i8]* @str, i64 0, i64 0), %3 ], [ getelementptr inbounds ([5 x i8], [5 x i8]* @str.2, i64 0, i64 0), %2 ]
  %6 = phi i32 [ 1, %3 ], [ 0, %2 ]
  %7 = tail call i32 @puts(i8* nonnull dereferenceable(1) %5)
  ret i32 %6
}

; Function Attrs: nofree nounwind
declare i32 @puts(i8* nocapture readonly) local_unnamed_addr #1

attributes #0 = { nofree nounwind uwtable "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { nofree nounwind }

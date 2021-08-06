; RUN: %outliningtest %s

; REQUIRES: llvm12
; REQUIRES: x86_64

source_filename = "build-llvm12/SingleSource/Benchmarks/BenchmarkGame/puzzle"

@next = internal unnamed_addr global i64 1, align 8
@.str = private unnamed_addr constant [21 x i8] c"Found duplicate: %d\0A\00", align 1

; Function Attrs: nofree norecurse nounwind uwtable willreturn
define dso_local i32 @rand() local_unnamed_addr #0 {
  %1 = load i64, i64* @next, align 8, !tbaa !7
  %2 = mul i64 %1, 1103515245
  %3 = add i64 %2, 12345
  store i64 %3, i64* @next, align 8, !tbaa !7
  %4 = lshr i64 %3, 16
  %5 = trunc i64 %4 to i32
  %6 = urem i32 %5, 32767
  %7 = add nuw nsw i32 %6, 1
  ret i32 %7
}

; Function Attrs: nofree norecurse nounwind uwtable willreturn writeonly
define dso_local void @srand(i32 %0) local_unnamed_addr #1 {
  %2 = zext i32 %0 to i64
  store i64 %2, i64* @next, align 8, !tbaa !7
  ret void
}

; Function Attrs: nofree norecurse nounwind uwtable willreturn
define dso_local i32 @randInt(i32 %0, i32 %1) local_unnamed_addr #0 {
  %3 = sub nsw i32 %1, %0
  %4 = add nsw i32 %3, 1
  %5 = sitofp i32 %4 to double
  %6 = load i64, i64* @next, align 8, !tbaa !7
  %7 = mul i64 %6, 1103515245
  %8 = add i64 %7, 12345
  store i64 %8, i64* @next, align 8, !tbaa !7
  %9 = lshr i64 %8, 16
  %10 = trunc i64 %9 to i32
  %11 = urem i32 %10, 32767
  %12 = add nuw nsw i32 %11, 1
  %13 = sitofp i32 %12 to double
  %14 = fmul double %13, 0x3F00000000000000
  %15 = fmul double %14, %5
  %16 = fptosi double %15 to i32
  %17 = icmp eq i32 %4, %16
  %18 = add nsw i32 %16, %0
  %19 = sext i1 %17 to i32
  %20 = add nsw i32 %18, %19
  ret i32 %20
}

; Function Attrs: nofree norecurse nounwind uwtable
define dso_local void @shuffle(i32* nocapture %0, i32 %1) local_unnamed_addr #2 {
  %3 = add nsw i32 %1, -1
  %4 = icmp eq i32 %3, 0
  br i1 %4, label %34, label %5

5:                                                ; preds = %2
  %6 = sext i32 %3 to i64
  %7 = load i64, i64* @next, align 8, !tbaa !7
  br label %8

8:                                                ; preds = %8, %5
  %9 = phi i64 [ %7, %5 ], [ %14, %8 ]
  %10 = phi i64 [ %6, %5 ], [ %31, %8 ]
  %11 = add i64 %10, 1
  %12 = uitofp i64 %11 to double
  %13 = mul i64 %9, 1103515245
  %14 = add i64 %13, 12345
  %15 = lshr i64 %14, 16
  %16 = trunc i64 %15 to i32
  %17 = urem i32 %16, 32767
  %18 = add nuw nsw i32 %17, 1
  %19 = sitofp i32 %18 to double
  %20 = fmul double %19, 0x3F00000000000000
  %21 = fmul double %20, %12
  %22 = fptosi double %21 to i32
  %23 = sext i32 %22 to i64
  %24 = icmp eq i64 %11, %23
  %25 = sext i1 %24 to i64
  %26 = add nsw i64 %25, %23
  %27 = getelementptr inbounds i32, i32* %0, i64 %10
  %28 = load i32, i32* %27, align 4, !tbaa !11
  %29 = getelementptr inbounds i32, i32* %0, i64 %26
  %30 = load i32, i32* %29, align 4, !tbaa !11
  store i32 %30, i32* %27, align 4, !tbaa !11
  store i32 %28, i32* %29, align 4, !tbaa !11
  %31 = add i64 %10, -1
  %32 = icmp eq i64 %31, 0
  br i1 %32, label %33, label %8, !llvm.loop !13

33:                                               ; preds = %8
  store i64 %14, i64* @next, align 8, !tbaa !7
  br label %34

34:                                               ; preds = %33, %2
  ret void
}

; Function Attrs: nofree nounwind uwtable
define dso_local noalias i32* @createRandomArray(i32 %0) local_unnamed_addr #3 {
  %2 = add i32 %0, 1
  %3 = sext i32 %2 to i64
  %4 = shl nsw i64 %3, 2
  %5 = tail call noalias i8* @malloc(i64 %4) #10
  %6 = bitcast i8* %5 to i32*
  %7 = icmp slt i32 %0, 0
  br i1 %7, label %81, label %8

8:                                                ; preds = %1
  %9 = zext i32 %2 to i64
  %10 = icmp ult i32 %2, 8
  br i1 %10, label %73, label %11

11:                                               ; preds = %8
  %12 = and i64 %9, 4294967288
  %13 = add nsw i64 %12, -8
  %14 = lshr exact i64 %13, 3
  %15 = add nuw nsw i64 %14, 1
  %16 = and i64 %15, 3
  %17 = icmp ult i64 %13, 24
  br i1 %17, label %54, label %18

18:                                               ; preds = %11
  %19 = and i64 %15, 4611686018427387900
  br label %20

20:                                               ; preds = %20, %18
  %21 = phi i64 [ 0, %18 ], [ %50, %20 ]
  %22 = phi <4 x i32> [ <i32 0, i32 1, i32 2, i32 3>, %18 ], [ %51, %20 ]
  %23 = phi i64 [ %19, %18 ], [ %52, %20 ]
  %24 = getelementptr inbounds i32, i32* %6, i64 %21
  %25 = add <4 x i32> %22, <i32 4, i32 4, i32 4, i32 4>
  %26 = bitcast i32* %24 to <4 x i32>*
  store <4 x i32> %22, <4 x i32>* %26, align 4, !tbaa !11
  %27 = getelementptr inbounds i32, i32* %24, i64 4
  %28 = bitcast i32* %27 to <4 x i32>*
  store <4 x i32> %25, <4 x i32>* %28, align 4, !tbaa !11
  %29 = or i64 %21, 8
  %30 = add <4 x i32> %22, <i32 8, i32 8, i32 8, i32 8>
  %31 = getelementptr inbounds i32, i32* %6, i64 %29
  %32 = add <4 x i32> %22, <i32 12, i32 12, i32 12, i32 12>
  %33 = bitcast i32* %31 to <4 x i32>*
  store <4 x i32> %30, <4 x i32>* %33, align 4, !tbaa !11
  %34 = getelementptr inbounds i32, i32* %31, i64 4
  %35 = bitcast i32* %34 to <4 x i32>*
  store <4 x i32> %32, <4 x i32>* %35, align 4, !tbaa !11
  %36 = or i64 %21, 16
  %37 = add <4 x i32> %22, <i32 16, i32 16, i32 16, i32 16>
  %38 = getelementptr inbounds i32, i32* %6, i64 %36
  %39 = add <4 x i32> %22, <i32 20, i32 20, i32 20, i32 20>
  %40 = bitcast i32* %38 to <4 x i32>*
  store <4 x i32> %37, <4 x i32>* %40, align 4, !tbaa !11
  %41 = getelementptr inbounds i32, i32* %38, i64 4
  %42 = bitcast i32* %41 to <4 x i32>*
  store <4 x i32> %39, <4 x i32>* %42, align 4, !tbaa !11
  %43 = or i64 %21, 24
  %44 = add <4 x i32> %22, <i32 24, i32 24, i32 24, i32 24>
  %45 = getelementptr inbounds i32, i32* %6, i64 %43
  %46 = add <4 x i32> %22, <i32 28, i32 28, i32 28, i32 28>
  %47 = bitcast i32* %45 to <4 x i32>*
  store <4 x i32> %44, <4 x i32>* %47, align 4, !tbaa !11
  %48 = getelementptr inbounds i32, i32* %45, i64 4
  %49 = bitcast i32* %48 to <4 x i32>*
  store <4 x i32> %46, <4 x i32>* %49, align 4, !tbaa !11
  %50 = add i64 %21, 32
  %51 = add <4 x i32> %22, <i32 32, i32 32, i32 32, i32 32>
  %52 = add i64 %23, -4
  %53 = icmp eq i64 %52, 0
  br i1 %53, label %54, label %20, !llvm.loop !15

54:                                               ; preds = %20, %11
  %55 = phi i64 [ 0, %11 ], [ %50, %20 ]
  %56 = phi <4 x i32> [ <i32 0, i32 1, i32 2, i32 3>, %11 ], [ %51, %20 ]
  %57 = icmp eq i64 %16, 0
  br i1 %57, label %71, label %58

58:                                               ; preds = %58, %54
  %59 = phi i64 [ %67, %58 ], [ %55, %54 ]
  %60 = phi <4 x i32> [ %68, %58 ], [ %56, %54 ]
  %61 = phi i64 [ %69, %58 ], [ %16, %54 ]
  %62 = getelementptr inbounds i32, i32* %6, i64 %59
  %63 = add <4 x i32> %60, <i32 4, i32 4, i32 4, i32 4>
  %64 = bitcast i32* %62 to <4 x i32>*
  store <4 x i32> %60, <4 x i32>* %64, align 4, !tbaa !11
  %65 = getelementptr inbounds i32, i32* %62, i64 4
  %66 = bitcast i32* %65 to <4 x i32>*
  store <4 x i32> %63, <4 x i32>* %66, align 4, !tbaa !11
  %67 = add i64 %59, 8
  %68 = add <4 x i32> %60, <i32 8, i32 8, i32 8, i32 8>
  %69 = add i64 %61, -1
  %70 = icmp eq i64 %69, 0
  br i1 %70, label %71, label %58, !llvm.loop !17

71:                                               ; preds = %58, %54
  %72 = icmp eq i64 %12, %9
  br i1 %72, label %81, label %73

73:                                               ; preds = %71, %8
  %74 = phi i64 [ 0, %8 ], [ %12, %71 ]
  br label %75

75:                                               ; preds = %75, %73
  %76 = phi i64 [ %79, %75 ], [ %74, %73 ]
  %77 = getelementptr inbounds i32, i32* %6, i64 %76
  %78 = trunc i64 %76 to i32
  store i32 %78, i32* %77, align 4, !tbaa !11
  %79 = add nuw nsw i64 %76, 1
  %80 = icmp eq i64 %79, %9
  br i1 %80, label %81, label %75, !llvm.loop !19

81:                                               ; preds = %75, %71, %1
  %82 = sitofp i32 %0 to double
  %83 = load i64, i64* @next, align 8, !tbaa !7
  %84 = mul i64 %83, 1103515245
  %85 = add i64 %84, 12345
  store i64 %85, i64* @next, align 8, !tbaa !7
  %86 = lshr i64 %85, 16
  %87 = trunc i64 %86 to i32
  %88 = urem i32 %87, 32767
  %89 = add nuw nsw i32 %88, 1
  %90 = sitofp i32 %89 to double
  %91 = fmul double %90, 0x3F00000000000000
  %92 = fmul double %91, %82
  %93 = fptosi double %92 to i32
  %94 = icmp eq i32 %93, %0
  %95 = add nsw i32 %93, 1
  %96 = sext i1 %94 to i32
  %97 = add nsw i32 %95, %96
  store i32 %97, i32* %6, align 4, !tbaa !11
  %98 = icmp eq i32 %0, 0
  br i1 %98, label %127, label %99

99:                                               ; preds = %81
  %100 = sext i32 %0 to i64
  br label %101

101:                                              ; preds = %101, %99
  %102 = phi i64 [ %85, %99 ], [ %107, %101 ]
  %103 = phi i64 [ %100, %99 ], [ %124, %101 ]
  %104 = add i64 %103, 1
  %105 = uitofp i64 %104 to double
  %106 = mul i64 %102, 1103515245
  %107 = add i64 %106, 12345
  %108 = lshr i64 %107, 16
  %109 = trunc i64 %108 to i32
  %110 = urem i32 %109, 32767
  %111 = add nuw nsw i32 %110, 1
  %112 = sitofp i32 %111 to double
  %113 = fmul double %112, 0x3F00000000000000
  %114 = fmul double %113, %105
  %115 = fptosi double %114 to i32
  %116 = sext i32 %115 to i64
  %117 = icmp eq i64 %104, %116
  %118 = sext i1 %117 to i64
  %119 = add nsw i64 %118, %116
  %120 = getelementptr inbounds i32, i32* %6, i64 %103
  %121 = load i32, i32* %120, align 4, !tbaa !11
  %122 = getelementptr inbounds i32, i32* %6, i64 %119
  %123 = load i32, i32* %122, align 4, !tbaa !11
  store i32 %123, i32* %120, align 4, !tbaa !11
  store i32 %121, i32* %122, align 4, !tbaa !11
  %124 = add i64 %103, -1
  %125 = icmp eq i64 %124, 0
  br i1 %125, label %126, label %101, !llvm.loop !13

126:                                              ; preds = %101
  store i64 %107, i64* @next, align 8, !tbaa !7
  br label %127

127:                                              ; preds = %126, %81
  ret i32* %6
}

; Function Attrs: inaccessiblememonly nofree nounwind willreturn
declare dso_local noalias i8* @malloc(i64) local_unnamed_addr #4

; Function Attrs: norecurse nounwind readonly uwtable
define dso_local i32 @findDuplicate(i32* nocapture readonly %0, i32 %1) local_unnamed_addr #5 {
  %3 = icmp sgt i32 %1, 0
  br i1 %3, label %4, label %98

4:                                                ; preds = %2
  %5 = zext i32 %1 to i64
  %6 = icmp ult i32 %1, 8
  br i1 %6, label %85, label %7

7:                                                ; preds = %4
  %8 = and i64 %5, 4294967288
  %9 = add nsw i64 %8, -8
  %10 = lshr exact i64 %9, 3
  %11 = add nuw nsw i64 %10, 1
  %12 = and i64 %11, 1
  %13 = icmp eq i64 %9, 0
  br i1 %13, label %58, label %14

14:                                               ; preds = %7
  %15 = and i64 %11, 4611686018427387902
  br label %16

16:                                               ; preds = %16, %14
  %17 = phi i64 [ 0, %14 ], [ %52, %16 ]
  %18 = phi <4 x i64> [ <i64 0, i64 1, i64 2, i64 3>, %14 ], [ %53, %16 ]
  %19 = phi <4 x i32> [ zeroinitializer, %14 ], [ %50, %16 ]
  %20 = phi <4 x i32> [ zeroinitializer, %14 ], [ %51, %16 ]
  %21 = phi i64 [ %15, %14 ], [ %54, %16 ]
  %22 = trunc <4 x i64> %18 to <4 x i32>
  %23 = add <4 x i32> %22, <i32 1, i32 1, i32 1, i32 1>
  %24 = trunc <4 x i64> %18 to <4 x i32>
  %25 = add <4 x i32> %24, <i32 5, i32 5, i32 5, i32 5>
  %26 = xor <4 x i32> %19, %23
  %27 = xor <4 x i32> %20, %25
  %28 = getelementptr inbounds i32, i32* %0, i64 %17
  %29 = bitcast i32* %28 to <4 x i32>*
  %30 = load <4 x i32>, <4 x i32>* %29, align 4, !tbaa !11
  %31 = getelementptr inbounds i32, i32* %28, i64 4
  %32 = bitcast i32* %31 to <4 x i32>*
  %33 = load <4 x i32>, <4 x i32>* %32, align 4, !tbaa !11
  %34 = xor <4 x i32> %26, %30
  %35 = xor <4 x i32> %27, %33
  %36 = or i64 %17, 8
  %37 = add <4 x i64> %18, <i64 8, i64 8, i64 8, i64 8>
  %38 = trunc <4 x i64> %37 to <4 x i32>
  %39 = add <4 x i32> %38, <i32 1, i32 1, i32 1, i32 1>
  %40 = trunc <4 x i64> %37 to <4 x i32>
  %41 = add <4 x i32> %40, <i32 5, i32 5, i32 5, i32 5>
  %42 = xor <4 x i32> %34, %39
  %43 = xor <4 x i32> %35, %41
  %44 = getelementptr inbounds i32, i32* %0, i64 %36
  %45 = bitcast i32* %44 to <4 x i32>*
  %46 = load <4 x i32>, <4 x i32>* %45, align 4, !tbaa !11
  %47 = getelementptr inbounds i32, i32* %44, i64 4
  %48 = bitcast i32* %47 to <4 x i32>*
  %49 = load <4 x i32>, <4 x i32>* %48, align 4, !tbaa !11
  %50 = xor <4 x i32> %42, %46
  %51 = xor <4 x i32> %43, %49
  %52 = add i64 %17, 16
  %53 = add <4 x i64> %18, <i64 16, i64 16, i64 16, i64 16>
  %54 = add i64 %21, -2
  %55 = icmp eq i64 %54, 0
  br i1 %55, label %56, label %16, !llvm.loop !21

56:                                               ; preds = %16
  %57 = trunc <4 x i64> %53 to <4 x i32>
  br label %58

58:                                               ; preds = %56, %7
  %59 = phi <4 x i32> [ undef, %7 ], [ %50, %56 ]
  %60 = phi <4 x i32> [ undef, %7 ], [ %51, %56 ]
  %61 = phi i64 [ 0, %7 ], [ %52, %56 ]
  %62 = phi <4 x i32> [ <i32 0, i32 1, i32 2, i32 3>, %7 ], [ %57, %56 ]
  %63 = phi <4 x i32> [ zeroinitializer, %7 ], [ %50, %56 ]
  %64 = phi <4 x i32> [ zeroinitializer, %7 ], [ %51, %56 ]
  %65 = icmp eq i64 %12, 0
  br i1 %65, label %79, label %66

66:                                               ; preds = %58
  %67 = getelementptr inbounds i32, i32* %0, i64 %61
  %68 = add <4 x i32> %62, <i32 5, i32 5, i32 5, i32 5>
  %69 = xor <4 x i32> %64, %68
  %70 = getelementptr inbounds i32, i32* %67, i64 4
  %71 = bitcast i32* %70 to <4 x i32>*
  %72 = load <4 x i32>, <4 x i32>* %71, align 4, !tbaa !11
  %73 = xor <4 x i32> %69, %72
  %74 = add <4 x i32> %62, <i32 1, i32 1, i32 1, i32 1>
  %75 = xor <4 x i32> %63, %74
  %76 = bitcast i32* %67 to <4 x i32>*
  %77 = load <4 x i32>, <4 x i32>* %76, align 4, !tbaa !11
  %78 = xor <4 x i32> %75, %77
  br label %79

79:                                               ; preds = %66, %58
  %80 = phi <4 x i32> [ %59, %58 ], [ %78, %66 ]
  %81 = phi <4 x i32> [ %60, %58 ], [ %73, %66 ]
  %82 = xor <4 x i32> %81, %80
  %83 = call i32 @llvm.vector.reduce.xor.v4i32(<4 x i32> %82)
  %84 = icmp eq i64 %8, %5
  br i1 %84, label %98, label %85

85:                                               ; preds = %79, %4
  %86 = phi i64 [ 0, %4 ], [ %8, %79 ]
  %87 = phi i32 [ 0, %4 ], [ %83, %79 ]
  br label %88

88:                                               ; preds = %88, %85
  %89 = phi i64 [ %91, %88 ], [ %86, %85 ]
  %90 = phi i32 [ %96, %88 ], [ %87, %85 ]
  %91 = add nuw nsw i64 %89, 1
  %92 = trunc i64 %91 to i32
  %93 = xor i32 %90, %92
  %94 = getelementptr inbounds i32, i32* %0, i64 %89
  %95 = load i32, i32* %94, align 4, !tbaa !11
  %96 = xor i32 %93, %95
  %97 = icmp eq i64 %91, %5
  br i1 %97, label %98, label %88, !llvm.loop !22

98:                                               ; preds = %88, %79, %2
  %99 = phi i32 [ 0, %2 ], [ %83, %79 ], [ %96, %88 ]
  %100 = xor i32 %99, %1
  ret i32 %100
}

; Function Attrs: nofree nosync nounwind readnone willreturn
declare i32 @llvm.vector.reduce.xor.v4i32(<4 x i32>) #6

; Function Attrs: nounwind uwtable
define dso_local i32 @main() local_unnamed_addr #7 {
  store i64 1, i64* @next, align 8, !tbaa !7
  br label %1

1:                                                ; preds = %131, %0
  %2 = phi i32 [ 0, %0 ], [ %139, %131 ]
  %3 = tail call noalias dereferenceable_or_null(2000004) i8* @malloc(i64 2000004) #10
  %4 = bitcast i8* %3 to i32*
  br label %5

5:                                                ; preds = %5, %1
  %6 = phi i64 [ 0, %1 ], [ %41, %5 ]
  %7 = phi <4 x i32> [ <i32 0, i32 1, i32 2, i32 3>, %1 ], [ %42, %5 ]
  %8 = getelementptr inbounds i32, i32* %4, i64 %6
  %9 = add <4 x i32> %7, <i32 4, i32 4, i32 4, i32 4>
  %10 = bitcast i32* %8 to <4 x i32>*
  store <4 x i32> %7, <4 x i32>* %10, align 4, !tbaa !11
  %11 = getelementptr inbounds i32, i32* %8, i64 4
  %12 = bitcast i32* %11 to <4 x i32>*
  store <4 x i32> %9, <4 x i32>* %12, align 4, !tbaa !11
  %13 = add nuw nsw i64 %6, 8
  %14 = add <4 x i32> %7, <i32 8, i32 8, i32 8, i32 8>
  %15 = getelementptr inbounds i32, i32* %4, i64 %13
  %16 = add <4 x i32> %7, <i32 12, i32 12, i32 12, i32 12>
  %17 = bitcast i32* %15 to <4 x i32>*
  store <4 x i32> %14, <4 x i32>* %17, align 4, !tbaa !11
  %18 = getelementptr inbounds i32, i32* %15, i64 4
  %19 = bitcast i32* %18 to <4 x i32>*
  store <4 x i32> %16, <4 x i32>* %19, align 4, !tbaa !11
  %20 = add nuw nsw i64 %6, 16
  %21 = add <4 x i32> %7, <i32 16, i32 16, i32 16, i32 16>
  %22 = getelementptr inbounds i32, i32* %4, i64 %20
  %23 = add <4 x i32> %7, <i32 20, i32 20, i32 20, i32 20>
  %24 = bitcast i32* %22 to <4 x i32>*
  store <4 x i32> %21, <4 x i32>* %24, align 4, !tbaa !11
  %25 = getelementptr inbounds i32, i32* %22, i64 4
  %26 = bitcast i32* %25 to <4 x i32>*
  store <4 x i32> %23, <4 x i32>* %26, align 4, !tbaa !11
  %27 = add nuw nsw i64 %6, 24
  %28 = add <4 x i32> %7, <i32 24, i32 24, i32 24, i32 24>
  %29 = getelementptr inbounds i32, i32* %4, i64 %27
  %30 = add <4 x i32> %7, <i32 28, i32 28, i32 28, i32 28>
  %31 = bitcast i32* %29 to <4 x i32>*
  store <4 x i32> %28, <4 x i32>* %31, align 4, !tbaa !11
  %32 = getelementptr inbounds i32, i32* %29, i64 4
  %33 = bitcast i32* %32 to <4 x i32>*
  store <4 x i32> %30, <4 x i32>* %33, align 4, !tbaa !11
  %34 = add nuw nsw i64 %6, 32
  %35 = add <4 x i32> %7, <i32 32, i32 32, i32 32, i32 32>
  %36 = getelementptr inbounds i32, i32* %4, i64 %34
  %37 = add <4 x i32> %7, <i32 36, i32 36, i32 36, i32 36>
  %38 = bitcast i32* %36 to <4 x i32>*
  store <4 x i32> %35, <4 x i32>* %38, align 4, !tbaa !11
  %39 = getelementptr inbounds i32, i32* %36, i64 4
  %40 = bitcast i32* %39 to <4 x i32>*
  store <4 x i32> %37, <4 x i32>* %40, align 4, !tbaa !11
  %41 = add nuw nsw i64 %6, 40
  %42 = add <4 x i32> %7, <i32 40, i32 40, i32 40, i32 40>
  %43 = icmp eq i64 %41, 500000
  br i1 %43, label %44, label %5, !llvm.loop !23

44:                                               ; preds = %5
  %45 = getelementptr inbounds i8, i8* %3, i64 2000000
  %46 = bitcast i8* %45 to i32*
  store i32 500000, i32* %46, align 4, !tbaa !11
  %47 = load i64, i64* @next, align 8, !tbaa !7
  %48 = mul i64 %47, 1103515245
  %49 = add i64 %48, 12345
  %50 = lshr i64 %49, 16
  %51 = trunc i64 %50 to i32
  %52 = urem i32 %51, 32767
  %53 = add nuw nsw i32 %52, 1
  %54 = sitofp i32 %53 to double
  %55 = fmul double %54, 0x3F00000000000000
  %56 = fmul double %55, 5.000000e+05
  %57 = fptosi double %56 to i32
  %58 = icmp eq i32 %57, 500000
  %59 = add nsw i32 %57, 1
  %60 = sext i1 %58 to i32
  %61 = add nsw i32 %59, %60
  store i32 %61, i32* %4, align 4, !tbaa !11
  br label %62

62:                                               ; preds = %62, %44
  %63 = phi i64 [ %49, %44 ], [ %68, %62 ]
  %64 = phi i64 [ 500000, %44 ], [ %85, %62 ]
  %65 = add nuw nsw i64 %64, 1
  %66 = uitofp i64 %65 to double
  %67 = mul i64 %63, 1103515245
  %68 = add i64 %67, 12345
  %69 = lshr i64 %68, 16
  %70 = trunc i64 %69 to i32
  %71 = urem i32 %70, 32767
  %72 = add nuw nsw i32 %71, 1
  %73 = sitofp i32 %72 to double
  %74 = fmul double %73, 0x3F00000000000000
  %75 = fmul double %74, %66
  %76 = fptosi double %75 to i32
  %77 = sext i32 %76 to i64
  %78 = icmp eq i64 %65, %77
  %79 = sext i1 %78 to i64
  %80 = add nsw i64 %79, %77
  %81 = getelementptr inbounds i32, i32* %4, i64 %64
  %82 = load i32, i32* %81, align 4, !tbaa !11
  %83 = getelementptr inbounds i32, i32* %4, i64 %80
  %84 = load i32, i32* %83, align 4, !tbaa !11
  store i32 %84, i32* %81, align 4, !tbaa !11
  store i32 %82, i32* %83, align 4, !tbaa !11
  %85 = add nsw i64 %64, -1
  %86 = icmp eq i64 %85, 0
  br i1 %86, label %87, label %62, !llvm.loop !13

87:                                               ; preds = %62
  store i64 %68, i64* @next, align 8, !tbaa !7
  br label %88

88:                                               ; preds = %128, %87
  %89 = phi i32 [ 0, %87 ], [ %129, %128 ]
  br label %90

90:                                               ; preds = %90, %88
  %91 = phi i64 [ 0, %88 ], [ %125, %90 ]
  %92 = phi <4 x i64> [ <i64 0, i64 1, i64 2, i64 3>, %88 ], [ %126, %90 ]
  %93 = phi <4 x i32> [ zeroinitializer, %88 ], [ %123, %90 ]
  %94 = phi <4 x i32> [ zeroinitializer, %88 ], [ %124, %90 ]
  %95 = trunc <4 x i64> %92 to <4 x i32>
  %96 = add <4 x i32> %95, <i32 1, i32 1, i32 1, i32 1>
  %97 = trunc <4 x i64> %92 to <4 x i32>
  %98 = add <4 x i32> %97, <i32 5, i32 5, i32 5, i32 5>
  %99 = xor <4 x i32> %93, %96
  %100 = xor <4 x i32> %94, %98
  %101 = getelementptr inbounds i32, i32* %4, i64 %91
  %102 = bitcast i32* %101 to <4 x i32>*
  %103 = load <4 x i32>, <4 x i32>* %102, align 4, !tbaa !11
  %104 = getelementptr inbounds i32, i32* %101, i64 4
  %105 = bitcast i32* %104 to <4 x i32>*
  %106 = load <4 x i32>, <4 x i32>* %105, align 4, !tbaa !11
  %107 = xor <4 x i32> %99, %103
  %108 = xor <4 x i32> %100, %106
  %109 = or i64 %91, 8
  %110 = add <4 x i64> %92, <i64 8, i64 8, i64 8, i64 8>
  %111 = trunc <4 x i64> %110 to <4 x i32>
  %112 = add <4 x i32> %111, <i32 1, i32 1, i32 1, i32 1>
  %113 = trunc <4 x i64> %110 to <4 x i32>
  %114 = add <4 x i32> %113, <i32 5, i32 5, i32 5, i32 5>
  %115 = xor <4 x i32> %107, %112
  %116 = xor <4 x i32> %108, %114
  %117 = getelementptr inbounds i32, i32* %4, i64 %109
  %118 = bitcast i32* %117 to <4 x i32>*
  %119 = load <4 x i32>, <4 x i32>* %118, align 4, !tbaa !11
  %120 = getelementptr inbounds i32, i32* %117, i64 4
  %121 = bitcast i32* %120 to <4 x i32>*
  %122 = load <4 x i32>, <4 x i32>* %121, align 4, !tbaa !11
  %123 = xor <4 x i32> %115, %119
  %124 = xor <4 x i32> %116, %122
  %125 = add nuw nsw i64 %91, 16
  %126 = add <4 x i64> %92, <i64 16, i64 16, i64 16, i64 16>
  %127 = icmp eq i64 %125, 500000
  br i1 %127, label %128, label %90, !llvm.loop !24

128:                                              ; preds = %90
  %129 = add nuw nsw i32 %89, 1
  %130 = icmp eq i32 %129, 200
  br i1 %130, label %131, label %88, !llvm.loop !25

131:                                              ; preds = %128
  %132 = xor <4 x i32> %124, %123
  %133 = call i32 @llvm.vector.reduce.xor.v4i32(<4 x i32> %132)
  %134 = getelementptr inbounds i8, i8* %3, i64 2000000
  %135 = bitcast i8* %134 to i32*
  %136 = load i32, i32* %135, align 4, !tbaa !11
  %137 = xor i32 %133, %136
  tail call void @free(i8* nonnull %3) #10
  %138 = tail call i32 (i8*, ...) @printf(i8* nonnull dereferenceable(1) getelementptr inbounds ([21 x i8], [21 x i8]* @.str, i64 0, i64 0), i32 %137)
  %139 = add nuw nsw i32 %2, 1
  %140 = icmp eq i32 %139, 5
  br i1 %140, label %141, label %1, !llvm.loop !26

141:                                              ; preds = %131
  ret i32 0
}

; Function Attrs: inaccessiblemem_or_argmemonly nounwind willreturn
declare dso_local void @free(i8* nocapture) local_unnamed_addr #8

; Function Attrs: nofree nounwind
declare dso_local i32 @printf(i8* nocapture readonly, ...) local_unnamed_addr #9

attributes #0 = { nofree norecurse nounwind uwtable willreturn "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { nofree norecurse nounwind uwtable willreturn writeonly "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #2 = { nofree norecurse nounwind uwtable "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #3 = { nofree nounwind uwtable "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #4 = { inaccessiblememonly nofree nounwind willreturn "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #5 = { norecurse nounwind readonly uwtable "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #6 = { nofree nosync nounwind readnone willreturn }
attributes #7 = { nounwind uwtable "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #8 = { inaccessiblemem_or_argmemonly nounwind willreturn "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #9 = { nofree nounwind "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #10 = { nounwind }

!llvm.ident = !{!0}
!llvm.module.flags = !{!1, !2, !3, !5}

!0 = !{!"clang version 12.0.0"}
!1 = !{i32 1, !"wchar_size", i32 4}
!2 = !{i32 2, !"bcdb.elf.type", i32 2}
!3 = !{i32 6, !"bcdb.elf.runpath", !4}
!4 = !{!"/nix/store/wfz1ijnmi8xrpw9bwivqih9szbbprfah-shell/lib64", !"/nix/store/wfz1ijnmi8xrpw9bwivqih9szbbprfah-shell/lib", !"/nix/store/sbbifs2ykc05inws26203h0xwcadnf0l-glibc-2.32-46/lib"}
!5 = !{i32 6, !"bcdb.elf.needed", !6}
!6 = !{!"libm.so.6", !"libc.so.6"}
!7 = !{!8, !8, i64 0}
!8 = !{!"long long", !9, i64 0}
!9 = !{!"omnipotent char", !10, i64 0}
!10 = !{!"Simple C/C++ TBAA"}
!11 = !{!12, !12, i64 0}
!12 = !{!"int", !9, i64 0}
!13 = distinct !{!13, !14}
!14 = !{!"llvm.loop.mustprogress"}
!15 = distinct !{!15, !14, !16}
!16 = !{!"llvm.loop.isvectorized", i32 1}
!17 = distinct !{!17, !18}
!18 = !{!"llvm.loop.unroll.disable"}
!19 = distinct !{!19, !14, !20, !16}
!20 = !{!"llvm.loop.unroll.runtime.disable"}
!21 = distinct !{!21, !14, !16}
!22 = distinct !{!22, !14, !20, !16}
!23 = distinct !{!23, !14, !16}
!24 = distinct !{!24, !14, !16}
!25 = distinct !{!25, !14}
!26 = distinct !{!26, !14}

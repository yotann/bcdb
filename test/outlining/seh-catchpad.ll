; RUN: %outliningtest --no-run %s

; based on llvm/test/CodeGen/X86/seh-catchpad.ll

target datalayout = "e-m:w-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-windows-msvc"

$"\01??_C@_07MKBLAIAL@finally?$AA@" = comdat any

$"\01??_C@_06IBDBCMGJ@caught?$AA@" = comdat any

@"\01??_C@_07MKBLAIAL@finally?$AA@" = linkonce_odr unnamed_addr constant [8 x i8] c"finally\00", comdat, align 1
@"\01??_C@_06IBDBCMGJ@caught?$AA@" = linkonce_odr unnamed_addr constant [7 x i8] c"caught\00", comdat, align 1

; Function Attrs: nounwind readnone
declare i32 @do_div(i32 %a, i32 %b) #0

define i32 @main() #1 personality i8* bitcast (i32 (...)* @__C_specific_handler to i8*) {
entry:
  %call = invoke i32 @do_div(i32 1, i32 0) #4
          to label %__try.cont.12 unwind label %catch.dispatch

__except.2:                                       ; preds = %__except
  %call4 = invoke i32 @do_div(i32 1, i32 0) #4
          to label %invoke.cont.3 unwind label %ehcleanup

invoke.cont.3:                                    ; preds = %__except.2
  invoke fastcc void @"\01?fin$0@0@main@@"() #4
          to label %__try.cont.12 unwind label %catch.dispatch.7

__except.9:                                       ; preds = %__except.ret
  %call11 = tail call i32 @puts(i8* nonnull getelementptr inbounds ([7 x i8], [7 x i8]* @"\01??_C@_06IBDBCMGJ@caught?$AA@", i64 0, i64 0))
  br label %__try.cont.12

__try.cont.12:                                    ; preds = %invoke.cont.3, %entry, %__except.9
  ret i32 0

catch.dispatch:                                   ; preds = %entry
  %cs1 = catchswitch within none [label %__except] unwind label %catch.dispatch.7

__except:                                         ; preds = %catch.dispatch
  %cp1 = catchpad within %cs1 [i8* null]
  catchret from %cp1 to label %__except.2

ehcleanup:                                        ; preds = %__except.2
  %cp2 = cleanuppad within none []
  invoke fastcc void @"\01?fin$0@0@main@@"() #4 [ "funclet"(token %cp2) ]
          to label %invoke.cont.6 unwind label %catch.dispatch.7

invoke.cont.6:                                    ; preds = %ehcleanup
  cleanupret from %cp2 unwind label %catch.dispatch.7

catch.dispatch.7:
  %cs2 = catchswitch within none [label %__except.ret] unwind to caller

__except.ret:                                     ; preds = %catch.dispatch.7
  %cp3 = catchpad within %cs2 [i8* bitcast (i32 (i8*, i8*)* @"\01?filt$0@0@main@@" to i8*)]
  catchret from %cp3 to label %__except.9
}

declare i32 @"\01?filt$0@0@main@@"(i8* nocapture readnone %exception_pointers, i8* nocapture readnone %frame_pointer) #1

declare i32 @filt() #1

declare i32 @__C_specific_handler(...)

declare fastcc void @"\01?fin$0@0@main@@"() #2

; Function Attrs: nounwind
declare i32 @puts(i8* nocapture readonly) #3

attributes #0 = { nounwind readnone "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="false" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "stack-protector-buffer-size"="8" "target-features"="+sse,+sse2" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="false" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "stack-protector-buffer-size"="8" "target-features"="+sse,+sse2" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #2 = { noinline nounwind "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="false" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "stack-protector-buffer-size"="8" "target-features"="+sse,+sse2" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #3 = { nounwind "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="false" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "stack-protector-buffer-size"="8" "target-features"="+sse,+sse2" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #4 = { noinline }
attributes #5 = { nounwind }

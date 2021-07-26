; RUN: %outliningtest --no-run %s

declare dso_local void @bar()
declare dso_local i32 @__CxxFrameHandler3(...)

define internal void @callee(i1 %cond) personality i8* bitcast (i32 (...)* @__CxxFrameHandler3 to i8*) {
entry:
  br i1 %cond, label %if.then, label %if.end

if.then:
  invoke void @bar()
          to label %invoke.cont unwind label %ehcleanup

invoke.cont:
  br label %try.cont

ehcleanup:
  %0 = cleanuppad within none []
  cleanupret from %0 unwind label %catch.dispatch

catch.dispatch:
  %1 = catchswitch within none [label %catch] unwind to caller

catch:
  %2 = catchpad within %1 [i8* null, i32 64, i8* null]
  catchret from %2 to label %catchret.dest

catchret.dest:
  br label %try.cont

try.cont:
  br label %if.end

if.end:
  ret void
}
; RUN: llvm-as < %s | bc-split -o %t
; RUN: llvm-dis < %t/functions/f.bc | FileCheck --check-prefix=DEFINE %s

; DEFINE: %0 = type { i8 }
; DEFINE: %1 = type { i16 }
; DEFINE: %2 = type { i32 }
; DEFINE: %3 = type { i64 }
; DEFINE: %4 = type { i128 }
%s = type { i8 }
%t = type { i16 }
%u = type { i32 }
%v = type { i64 }
%w = type { i128 }

define void @f() personality i8 ()* null {
  call void @g(%s* null, %t* null)
  call void() bitcast (void(%w*)* @h to void()*)()
  call void undef(%u* byval null)
  invoke void undef(%v* inalloca null) to label %to unwind label %exc

to:
  ret void

exc:
  landingpad i8 cleanup
  ret void
}

define void @g(%s* byval, %t* inalloca) {
  ret void
}

define void @h(%w* byval) {
  ret void
}

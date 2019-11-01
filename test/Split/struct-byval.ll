; RUN: llvm-as < %s | bc-split -o %t
; RUN: llvm-dis < %t/functions/f.bc | FileCheck --check-prefix=DEFINE %s

; DEFINE-DAG: %{{[a-zA-Z0-9_.]+}} = type { i8 }
; DEFINE-DAG: %{{[a-zA-Z0-9_.]+}} = type { i16 }
; DEFINE-DAG: %{{[a-zA-Z0-9_.]+}} = type { i32 }
; DEFINE-DAG: %{{[a-zA-Z0-9_.]+}} = type { i64 }
; DEFINE-DAG: %{{[a-zA-Z0-9_.]+}} = type { i128 }
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

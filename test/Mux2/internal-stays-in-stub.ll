; RUN: bcdb init -uri sqlite:%t.bcdb
; RUN: bcdb add -uri sqlite:%t.bcdb %s -name prog
; RUN: bcdb mux2 -uri sqlite:%t.bcdb prog -o %t --muxed-name=libmuxed.so --weak-name=libweak.so
; RUN: opt -verify -S < %t/prog | FileCheck --check-prefix=PROG %s

@Py_DebugFlag = weak global i32 0, align 4

define i32 @Py_Main() {
  %x = call i32 @pymain_main()
  ret i32 %x
}

define internal fastcc i32 @pymain_main() {
  %x = load i32, i32* @Py_DebugFlag
  ret i32 %x
}

; PROG: @Py_DebugFlag = weak global i32 0, align 4
; PROG: define protected fastcc i32 @__bcdb_private_pymain_main()
; PROG-NEXT: call fastcc i32 @__bcdb_body_pymain_main()

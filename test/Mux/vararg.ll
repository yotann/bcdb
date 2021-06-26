; RUN: memodb init -store sqlite:%t
; RUN: llvm-as < %s | bcdb add -store sqlite:%t - -name x
; RUN: bcdb mux -store sqlite:%t x | lli - x

; RUN: memodb init -store sqlite:%t.rg
; RUN: llvm-as < %s | bcdb add -store sqlite:%t.rg - -name x -rename-globals
; RUN: bcdb mux -store sqlite:%t.rg x | lli - x

%struct.va_list = type { i32, i32, i8*, i8* }

@flag = internal global i32 13

declare void @llvm.va_start(i8*)
declare void @llvm.va_end(i8*)

define void @func(...) {
  %ap = alloca %struct.va_list
  %ap2 = bitcast %struct.va_list* %ap to i8*
  call void @llvm.va_start(i8* %ap2)
  %x = va_arg i8* %ap2, i32*
  %y = va_arg i8* %ap2, i32
  call void @llvm.va_end(i8* %ap2)
  %z = load i32, i32* %x
  %w = sub i32 %z, %y
  store i32 %w, i32* %x
  ret void
}

define i32 @main(i32, i8**) {
  call void(...) @func(i32* @flag, i32 13)
  %flag = load i32, i32* @flag
  ret i32 %flag
}

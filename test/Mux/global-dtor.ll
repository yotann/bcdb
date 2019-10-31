; RUN: bcdb init -uri sqlite:%t
; RUN: llvm-as < %s | bcdb add -uri sqlite:%t - -name x
; RUN: bcdb mux -uri sqlite:%t x | lli - x

%0 = type { i32, void ()*, i8* }

@llvm.global_dtors = appending global [3 x %0] [
  %0 { i32 65535, void ()* @dtor0, i8* null },
  %0 { i32 65535, void ()* @dtor0, i8* null },
  %0 { i32 65535, void ()* @dtor1, i8* null }
]
@flag = internal global i32 2

declare void @_exit(i32)

define void @dtor0() {
  %1 = load i32, i32* @flag
  %2 = sub i32 %1, 1
  store i32 %2, i32* @flag
  ret void
}

define void @dtor1() {
  %1 = load i32, i32* @flag
  call void @_exit(i32 %1)
  unreachable
}

define i32 @main(i32, i8**) {
  ret i32 1
}

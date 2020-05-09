; RUN: %mux2test %s %t
; RUN: %t.elf/module x y

declare void @exit(i32)
declare void @raise(i32)

define i32 @main(i32 %argc, i8**) {
  %cond = icmp ult i32 %argc, 2
  %str = select i1 %cond, { void(i32)*, i32 } { void(i32)* @raise, i32 1 }, { void(i32)*, i32 } { void(i32)* @exit, i32 0 }
  %func = extractvalue { void(i32)*, i32 } %str, 0
  %val = extractvalue { void(i32)*, i32 } %str, 1
  call void %func(i32 %val)
  ret i32 1
}

define i32 @foo(i32 %argc, i8**) {
  %func = extractvalue [2 x void(i32)*] [ void(i32)* @raise, void(i32)* @exit ], 1
  call void %func(i32 0)
  ret i32 1
}

define i32 @bar(i32 %argc, i8**) {
  %func = extractelement <2 x void(i32)*> < void(i32)* @raise, void(i32)* @exit >, i32 1
  call void %func(i32 0)
  ret i32 1
}

!llvm.module.flags = !{!1}
!1 = !{i32 2, !"bcdb.elf.type", i32 2}

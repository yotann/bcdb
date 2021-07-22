// RUN: %gltest %s %t
// RUN: %t.elf/module2
// RUN: %t.elf/module3

#if MODULE0

@global = global i32 1

declare extern_weak void @extern()

define void @extern_caller() {
  call void @_nc_render()
  call void @extern()
  ret void
}

define void @_nc_render() local_unnamed_addr {
  store i32 2, i32* @global
  ret void
}

define i32 @wborder() local_unnamed_addr {
  call void @_nc_render()
  %x = load i32, i32* @global
  ret i32 %x
}

define i32 @border() local_unnamed_addr {
  %x = call i32 @wborder()
  ret i32 %x
}

!llvm.module.flags = !{!1, !2}
!1 = !{i32 2, !"bcdb.elf.type", i32 3}
!2 = !{i32 7, !"PIC Level", i32 2}



#elif MODULE1

@global = global i32 3

define void @_nc_render() {
  store i32 4, i32* @global
  ret void
}

define i32 @wborder() {
  call void @_nc_render()
  %x = load i32, i32* @global
  ret i32 %x
}

!llvm.module.flags = !{!1, !2}
!1 = !{i32 2, !"bcdb.elf.type", i32 3}
!2 = !{i32 7, !"PIC Level", i32 2}



#elif MODULE2

declare i32 @wborder()

define i32 @main() {
  %x = call i32 @wborder()
  %y = sub i32 %x, 2
  ret i32 %y
}

!llvm.module.flags = !{!1, !2}
!1 = !{i32 2, !"bcdb.elf.type", i32 2}
!2 = !{i32 6, !"bcdb.elf.needed", !3}
!3 = !{!"module0"}



#elif MODULE3

declare i32 @wborder()

define i32 @main() {
  %x = call i32 @wborder()
  %y = sub i32 %x, 4
  ret i32 %y
}

!llvm.module.flags = !{!1, !2}
!1 = !{i32 2, !"bcdb.elf.type", i32 2}
!2 = !{i32 6, !"bcdb.elf.needed", !3}
!3 = !{!"module1"}



#endif

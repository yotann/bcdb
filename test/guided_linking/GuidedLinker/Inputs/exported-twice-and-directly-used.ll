@exported_constant = weak constant i32 -12

define weak i32 @exported_func() {
  ret i32 12
}

define i32 @weak_user() {
  call i32 @exported_func()
  %x = load i32, i32* @exported_constant
  ret i32 %x
}

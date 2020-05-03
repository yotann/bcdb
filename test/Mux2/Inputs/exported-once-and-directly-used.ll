@exported_constant = private constant i32 -12

define private i32 @exported_func() {
  ret i32 12
}

define i32 @private_user() {
  call i32 @exported_func()
  %x = load i32, i32* @exported_constant
  ret i32 %x
}

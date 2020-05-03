@exported_constant = external constant i32

declare i32 @exported_func()

define i32 @imported_user() {
  call i32 @exported_func()
  %x = load i32, i32* @exported_constant
  ret i32 %x
}

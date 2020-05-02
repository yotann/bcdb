$g = comdat any

define linkonce_odr void @g() comdat {
  call void @g()
  ret void
}

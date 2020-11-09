$g = comdat any

declare void @maybe_abort()

define weak_odr void @g() comdat {
  call void @maybe_abort()
  call void @g()
  ret void
}

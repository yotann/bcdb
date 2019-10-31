@global = global i32 1

define void @set_global_to_0() {
  store i32 0, i32* @global
  ret void
}

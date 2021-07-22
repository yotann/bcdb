@global = common global i32 0

define void @set_global_to_1() {
  store i32 1, i32* @global
  ret void
}

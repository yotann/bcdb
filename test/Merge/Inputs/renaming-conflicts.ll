@g1 = internal constant i8* bitcast (void()* @f1 to i8*)
@g0 = internal constant i8* bitcast (void()* @f0 to i8*)

define internal void @f1() {
  ret void
}

define internal void @f0() {
  ret void
}

define void @main() {
  load i8*, i8** @g1
  load i8*, i8** @g0
  call void @f1()
  call void @f0()
  ret void
}

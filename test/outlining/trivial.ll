; RUN: %outliningtest %s

define i32 @main() {
  call i32 @putchar(i32 72)
  call i32 @putchar(i32 101)
  call i32 @putchar(i32 108)
  call i32 @putchar(i32 108)
  call i32 @putchar(i32 111)
  call i32 @putchar(i32 10)
  call i32 @putchar(i32 72)
  call i32 @putchar(i32 101)
  call i32 @putchar(i32 108)
  call i32 @putchar(i32 108)
  call i32 @putchar(i32 111)
  call i32 @putchar(i32 10)
  call i32 @putchar(i32 72)
  call i32 @putchar(i32 101)
  call i32 @putchar(i32 108)
  call i32 @putchar(i32 108)
  call i32 @putchar(i32 111)
  call i32 @putchar(i32 10)
  ret i32 0
}

declare i32 @putchar(i32)

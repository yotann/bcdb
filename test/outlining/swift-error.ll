; XFAIL: *

; RUN: opt -load %shlibdir/BCDBOutliningPlugin%shlibext \
; RUN:     -outlining-extractor -outline-unprofitable -verify -S %s

%swift.error = type opaque

declare swiftcc void @f(%swift.error** swifterror)

define swiftcc void @g(i8*, i8*, i8*, i8*, %swift.error** swifterror %error) {
entry:
  call swiftcc void @f(%swift.error** nonnull nocapture swifterror %error)
  ret void
}

define void @creates_swifterror() {
  %error_ptr_ref = alloca swifterror %swift.error*
  store %swift.error* null, %swift.error** %error_ptr_ref
  call swiftcc void @f(%swift.error** swifterror %error_ptr_ref)
  ret void
}

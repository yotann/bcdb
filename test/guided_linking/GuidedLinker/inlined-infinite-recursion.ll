; RUN: memodb init -store sqlite:%t.bcdb
; RUN: bcdb add -store sqlite:%t.bcdb %s -name mod
; RUN: bcdb gl -store sqlite:%t.bcdb mod -o %t --disable-dso-local --merged-name=libmerged.so --noweak --nooverride
; RUN: bcdb gl -store sqlite:%t.bcdb mod -o %t --disable-dso-local --merged-name=libmerged.so --noweak --nooverride --nouse --noplugin

define available_externally void @callee() alwaysinline {
  call void @callee()
  ret void
}

define void @caller() {
  call void @callee()
  ret void
}

; RUN: bcdb init -uri sqlite:%t.bcdb
; RUN: bcdb add -uri sqlite:%t.bcdb %s -name mod
; RUN: bcdb gl -uri sqlite:%t.bcdb mod -o %t --disable-dso-local --merged-name=libmerged.so --noweak --nooverride
; RUN: bcdb gl -uri sqlite:%t.bcdb mod -o %t --disable-dso-local --merged-name=libmerged.so --noweak --nooverride --nouse --noplugin

define available_externally void @callee() alwaysinline {
  call void @callee()
  ret void
}

define void @caller() {
  call void @callee()
  ret void
}

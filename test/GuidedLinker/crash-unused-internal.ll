; RUN: bcdb init -uri sqlite:%t.bcdb
; RUN: bcdb add -uri sqlite:%t.bcdb %s -name prog
; RUN: bcdb gl -uri sqlite:%t.bcdb prog -o %t --muxed-name=libmuxed.so --noweak

define internal void @_PyTime_GetProcessTimeWithInfo() unnamed_addr {
  ret void
}

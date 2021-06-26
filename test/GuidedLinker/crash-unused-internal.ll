; RUN: memodb init -store sqlite:%t.bcdb
; RUN: bcdb add -store sqlite:%t.bcdb %s -name prog
; RUN: bcdb gl -store sqlite:%t.bcdb prog -o %t --merged-name=libmerged.so --noweak

define internal void @_PyTime_GetProcessTimeWithInfo() unnamed_addr {
  ret void
}

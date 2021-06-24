%struct = type { i32 }

define void @caller() {
  call void @callee(%struct* sret(%struct) undef)
  unreachable
}

declare void @callee(%struct* sret(%struct))

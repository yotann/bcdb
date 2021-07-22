%struct1 = type { i64 }

define void @caller1() {
  call void @callee(%struct1* sret(%struct1) undef)
  unreachable
}

declare void @callee(%struct1* sret(%struct1))

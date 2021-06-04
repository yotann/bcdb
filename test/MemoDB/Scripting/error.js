// RUN: not --crash memodb js %s 2>&1 | FileCheck %s
// CHECK: Error: error
throw new Error("error");

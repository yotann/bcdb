#ifndef MEMODB_TESTINGSUPPORT_H
#define MEMODB_TESTINGSUPPORT_H

#include <iostream>

#include <llvm/ADT/StringRef.h>
#include <llvm/Support/raw_os_ostream.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

MATCHER_P(TwineEq, string, "") {
  return llvm::StringRef(string).equals(arg.str());
}

MATCHER_P(TwineCaseEq, string, "") {
#if LLVM_VERSION_MAJOR >= 13
  return llvm::StringRef(string).equals_insensitive(arg.str());
#else
  return llvm::StringRef(string).equals_lower(arg.str());
#endif
}

} // end anonymous namespace

namespace llvm {

// Allow googletest to print values that only support llvm::raw_ostream.
template <typename T>
std::ostream &operator<<(std::ostream &os, const T &value) {
  llvm::raw_os_ostream raw_os(os);
  raw_os << '"' << value << '"';
  return os;
}

} // namespace llvm

#endif // MEMODB_TESTINGSUPPORT_H

// RUN: clang++ %s -fembed-bitcode -O1 -o %t.orig
// RUN: bc-imitate extract %t.orig | llvm-dis > %t.ll
// RUN: %gltest %t.ll %t
// RUN: %t.elf/module
// RUN: %gltest %t.ll %t --noweak
// RUN: %t.elf/module
// RUN: %gltest %t.ll %t --noweak --nooverride
// RUN: %t.elf/module
// RUN: %gltest %t.ll %t --noweak --nooverride --nouse
// RUN: %t.elf/module
// FIXME: run with --noplugin (requires exceptions list based on libstdc++.a)

int throws() {
  throw 12;
  return 1;
}

int main() {
  try {
    return throws();
  } catch (int i) {
    return i - 12;
  }
}

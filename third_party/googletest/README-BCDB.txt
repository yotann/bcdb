This directory contains Google Test downloaded from
https://github.com/google/googletest on 2021-06-01.

Unnecessary files have been removed.

The following line has been removed from src/gtest_main.cc, because it confuses lit:
  printf("Running main() from %s\n", __FILE__);

The options "-Werror" and "-WX" have been removed from cmake/internal_utils.cmake.

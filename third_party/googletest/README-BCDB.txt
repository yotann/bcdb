This directory contains Google Test downloaded from
https://github.com/google/googletest on 2020-02-15.

Unnecessary files have been removed.

The following line has been removed from src/gtest_main.cc, because it confuses lit:
  printf("Running main() from %s\n", __FILE__);

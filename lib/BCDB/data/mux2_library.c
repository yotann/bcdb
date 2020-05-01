// This file provides extra code used in the muxed library created by
// bcdb mux2.

#include <stdio.h>
#include <stdlib.h>

static void check_envvars() __attribute__((constructor(101)));
static void check_envvars() {
  if (!getenv("LD_DYNAMIC_WEAK")) {
    fprintf(stderr, "error: you must set the LD_DYNAMIC_WEAK "
                    "environment variable before running this program.\n");
    abort();
  }
}

void __bcdb_weak_definition_called(const char *name) {
  fprintf(stderr, "error: called weak placeholder definition \"%s\"\n", name);
  abort();
}

void __bcdb_unreachable_function_called(const char *name) {
  fprintf(stderr, "error: called unreachable function \"%s\"\n", name);
  abort();
}

// This file provides extra code used in the muxed library created by
// bcdb mux2.

#include <stdio.h>
#include <stdlib.h>

void __bcdb_weak_definition_called(const char *name) {
  fprintf(stderr, "error: called weak placeholder definition \"%s\"\n", name);
  abort();
}

void __bcdb_unreachable_function_called(const char *name) {
  fprintf(stderr, "error: called unreachable function \"%s\"\n", name);
  abort();
}

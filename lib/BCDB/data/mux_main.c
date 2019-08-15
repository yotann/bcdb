// This file provides the implementation of main() used by bcdb mux.
// If this file is changed, you must regenerate the mux_main.inc file:
//   clang-4.0 -emit-llvm -Os -c mux_main.c
//   xxd -i mux_main.bc > mux_main.inc

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *basename(const char *name) {
  const char *p = strrchr(name, '/');
  if (p)
    return p + 1;
  return name;
}

struct Main {
  const char *name;
  int (*main)(int, char **, char **);
  void (**init)(void);
  void (**fini)(void);
};

extern struct Main __bcdb_main;

static void (**fini)(void);

void do_fini(void) {
  void (**ptr)(void);
  ptr = fini;
  while (*ptr) {
    (*ptr++)();
  }
}

static void try_main(int argc, char *argv[], char *envp[]) {
  const char *name = basename(argv[0]);
  struct Main *ptr;
  for (ptr = &__bcdb_main; ptr->name; ptr++) {
    if (!strcmp(name, ptr->name)) {
      fini = ptr->init;
      do_fini();
      fini = ptr->fini;
      atexit(do_fini);
      exit(ptr->main(argc, argv, envp));
    }
  }
}

int main(int argc, char *argv[],
         char *envp[] /* not POSIX but needed by some things */) {

  // If the user is running /bin/foo arg1 arg2
  try_main(argc, argv, envp);

  // If the user is running /bin/muxed foo arg1 arg2
  if (argc > 1)
    try_main(argc - 1, argv + 1, envp);

  // No subcommand specified. Print a list of available subcommands.
  struct Main *ptr;
  for (ptr = &__bcdb_main; ptr->name; ptr++) {
    puts(ptr->name);
  }
  return -1;
}

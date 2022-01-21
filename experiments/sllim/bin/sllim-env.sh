#!/bin/sh

# NOTE: this file should be sourced from an interactive shell:
#   user@host$ . sllim-env.sh

# This script should work on systems with shells other than Bash (Dash, Busybox
# Ash, etc.) and with optional programs missing (such as "which").

SLLIM_OLD_OPTS="$(set +o)"

set -eu

realdir() {
  cd "$1"
  pwd -P
}

path() {
  if ! SLLIM_TMP="$(command -v "$2")"; then
    echo "error: command '$2' not found" >&2
    exit 1
  fi
  export "$1=$SLLIM_TMP"
}

# Prevent nested usage of sllim-env.
if [ -n "${SLLIM_IN_ENV+set}" ]; then
  echo "error: nesting sllim-env is not supported" >&2
  exit 1
fi
export SLLIM_IN_ENV=1

# Set SLLIM_TTY to the current stderr.
# That way, even if autoconf runs $CC with a redirected stderr, we can still
# print messages to the original stderr.
if [ -L "/proc/$$/fd/2" ]; then
  SLLIM_TTY="$(readlink "/proc/$$/fd/2")"
  export SLLIM_TTY
fi

# Find the path to the sllim scripts.
SLLIM_PATH="$(realdir "$(dirname "$(command -v sllim-env)")")"

# Check that the directory containing our versions of "cc", "gcc", etc. is
# available.
SLLIM_ENV_PATH="$(dirname "$SLLIM_PATH")/libexec/sllim/env"
if ! [ -d "$SLLIM_ENV_PATH" ]; then
  echo "error: sllim-env install directory missing" >&2
  echo "  (expected at $SLLIM_ENV_PATH)" >&2
  exit 1
fi

# Find paths to the original tools before we override them.
path SLLIM sllim
path SLLIM_BC_IMITATE bc-imitate
path SLLIM_CLANG clang
path SLLIM_CLANGXX clang++
export SLLIM_LD="$("$SLLIM_CLANG" -print-prog-name=ld)"

# Use our own wrapper scripts.
path CC sllim-cc
path CXX sllim-c++
path LD sllim-ld

# Use the LLVM versions of these, just like emscripten does.
path AR llvm-ar
path NM llvm-nm
path RANLIB llvm-ranlib

# Add our own "cc", "gcc", etc. to PATH.
export PATH="$SLLIM_ENV_PATH${PATH:+:${PATH}}"

# Change the prompt to make it clear that sllim-env is active.
export PS1="sllim-env: ${PS1-\$ }"

echo "SLLIM overrides added."

eval "$SLLIM_OLD_OPTS"
unset SLLIM_OLD_OPTS

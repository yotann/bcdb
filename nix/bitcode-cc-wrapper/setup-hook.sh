#!/usr/bin/env bash

# Set BITCODE_WRAPPER_TTY to the current stderr.
# That way, even if clang is run with a redirected stderr, we can still print
# messages to the original stderr.
export BITCODE_WRAPPER_TTY=/proc/$$/fd/2

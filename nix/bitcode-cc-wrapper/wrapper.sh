#! @shell@
# shellcheck shell=bash
set -eu -o pipefail +o posix
shopt -s nullglob

log() {
  # See setup-hook.sh
  if [ -n "${BITCODE_WRAPPER_TTY:-}" ]; then
    echo "$@" >> "$BITCODE_WRAPPER_TTY"
  else
    echo "$@" >&2
  fi
}

# Clang can try to invoke GCC when e.g. compiling an Ada source file, which can
# lead to infinite recursion if it actually invokes this script. We detect
# recursion and return an error.
if [ -n "${ALREADY_IN_BITCODE_WRAPPER:-}" ]; then
  log Bitcode wrapper called recursively: "@dst@" "$@"
  exit 1
fi
export ALREADY_IN_BITCODE_WRAPPER=1

declare -a processed_args
process_cc_flags() {
  if [ "$*" = -v ] || [ "$*" = --version ]; then
    processed_args=("$*")
    return
  fi
  while [ $# -gt 0 ]; do
    case "$1" in
      # Disable debugging info. Otherwise we end up with executable code,
      # bitcode, and debug info for both in the same file, which makes the file
      # excessively large.
      -g|-g[0-9])
        shift
        ;;

      # Clang doesn't support these options with -fembed-bitcode.
      -Wa,--noexecstack|-fdata-sections|-ffunction-sections|-mno-red-zone|-mstackrealign)
        shift
        ;;

      # This option would prevent the .llvmbc section from being generated.
      -flto)
        shift
        ;;

      # This option would delete the .llvmbc section.
      -Wl,--gc-sections)
        shift
        ;;

      # Clang may find warnings in code that's fine in GCC, so prevent treating
      # them as errors.
      -Werror*)
        shift
        ;;

      # All other options are passed through unmodified.
      *)
        processed_args+=("$1")
        shift
        ;;
    esac
  done

  # Embed the bitcode!
  processed_args+=("-fembed-bitcode=all")
}

process_cc_flags "$@"
RC=0
"@prog@" "${processed_args[@]}" || RC=$?
if [ "$RC" -ne 0 ]; then
  log Original command line: "@dst@" "$@"
  log Modified command line: "@prog@" "${processed_args[@]}"
fi
exit "$RC"

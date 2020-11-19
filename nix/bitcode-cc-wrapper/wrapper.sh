#! @shell@
set -eu -o pipefail +o posix
shopt -s nullglob

log() {
  # See setup-hook.sh
  if [ -n "${BITCODE_WRAPPER_TTY:-}" ]; then
    echo $@ >> $BITCODE_WRAPPER_TTY
  else
    echo $@ >&2
  fi
}

declare -a processed_args
process_cc_flags() {
  if [ "$*" = -v ] || [ "$*" = --version ]; then
    processed_args=("$*")
    return
  fi
  while [ $# -gt 0 ]; do
    case "$1" in
      # disable debugging info
      -g|-g[0-9])
        shift
        ;;

      # conflicts with -fembed-bitcode
      -Wa,--noexecstack)
        shift
        ;;

      # conflicts with -fembed-bitcode
      -flto)
        shift
        ;;

      # conflicts with -fembed-bitcode
      -ffunction-sections|-fdata-sections)
        shift
        ;;

      # deletes the .llvmbc section
      -Wl,--gc-sections)
        shift
        ;;

      # can cause errors in Clang even if the code is fine in GCC
      -Werror*)
        shift
        ;;

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
  log Command line: "@dst@" "$@"
  log "@prog@" "${processed_args[@]}"
fi
exit "$RC"

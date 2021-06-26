#!/bin/sh
set -eu
# shellcheck disable=SC2046
nix-store --realise $(nix-store -q --references $(nix-instantiate) | grep -v 'bcdb$') | cachix push bcdb

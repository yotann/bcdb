#!/usr/bin/env bash
set -eu

nix-store -q --references $(nix-instantiate -A $A) | grep -v 'bcdb$' > .deps.txt
KEY=nix-realise-$(checksum .deps.txt)

sudo mv /nix /nix.old
cache restore $KEY
if ! [ -d /nix ]; then
  sudo mv /nix.old /nix

  nix-store --realise $(cat .deps.txt) | cachix push bcdb
  cache store $KEY-$SEMAPHORE_JOB_ID /nix
  cache list
fi

#!/usr/bin/env bash
set -eu

nix-store -q --references $(nix-instantiate -A $A) | grep -v 'bcdb$' > .deps.txt
KEY=nix-realise-$(checksum .deps.txt)

cache restore $KEY
if [ -d nix ]; then
  sudo mv /nix /nix.old
  sudo mv nix /nix
else
  nix-store --realise $(cat .deps.txt) | cachix push bcdb
  cache store $KEY-$SEMAPHORE_JOB_ID /nix
  cache list
fi

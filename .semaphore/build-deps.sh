#!/bin/sh
set -eu
nix-store -q --references $(nix-instantiate -A $ATTR) | grep -v 'bcdb$' > .deps.txt
KEY=nix-realise-$(checksum .deps.txt)
if cache has_key $KEY; then
  cache restore $KEY
  sudo mv /nix /nix.old
  sudo mv nix /nix
else
  nix-store --realise $(cat .deps.txt) | cachix push bcdb
  cache store $KEY /nix
fi

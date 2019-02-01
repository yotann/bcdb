#!/usr/bin/env bash
set -eu

PROFILE=/nix/var/nix/profiles/per-user/$USER/profile/etc/profile.d/nix.sh

cache restore nix-with-cachix
if [ -d nix ]; then
  # Restore Nix from cache.
  sudo mv nix /nix
  . $PROFILE
  ln -s /nix/var/nix/profiles/per-user/$USER/channels ~/.nix-defexpr/
else
  # Install Nix and Cachix.
  bash <(curl https://nixos.org/nix/install)
  . $PROFILE
  nix-env -iA cachix -f https://cachix.org/api/v1/install
  cache store nix-with-cachix-$SEMAPHORE_JOB_ID /nix
  cache list
fi

cachix use bcdb
echo "build-cores $(($(nproc)*2))" >> ~/.config/nix/nix.conf

#!/usr/bin/env bash
set -eu

PROFILE=~/.nix-profile/etc/profile.d/nix.sh

sudo chmod 777 / # Allow cache restore to create /nix
cache restore nix-with-cachix
if [ -d /nix ]; then
  # Restore Nix from cache.
  mkdir ~/.nix-defexpr
  ln -s /nix/var/nix/profiles/per-user/$USER/profile ~/.nix-profile
  ln -s /nix/var/nix/profiles/per-user/$USER/channels ~/.nix-defexpr/
  . $PROFILE
else
  # Install Nix and Cachix.
  bash <(curl https://nixos.org/nix/install)
  . $PROFILE
  nix-env -iA cachix -f https://cachix.org/api/v1/install
  nix-collect-garbage
  cache store nix-with-cachix-$SEMAPHORE_JOB_ID /nix
  cache list
fi

cachix use bcdb
echo "cores = $(($(nproc)*2))" >> ~/.config/nix/nix.conf

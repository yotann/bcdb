#!/usr/bin/env bash
set -eu

PROFILE=/nix/var/nix/profiles/per-user/$USER/profile/etc/profile.d/nix.sh

if cache has_key nix-with-cachix; then
  # Restore Nix from cache.
  cache restore nix-with-cachix
  sudo mv nix /nix
  . $PROFILE
  ln -s /nix/var/nix/profiles/per-user/$USER/channels ~/.nix-defexpr/
else
  # Install Nix and Cachix.
  bash <(curl https://nixos.org/nix/install)
  . $PROFILE
  nix-env -iA cachix -f https://cachix.org/api/v1/install
  cache store nix-with-cachix /nix
  cache list
fi

cachix use bcdb
echo "build-cores $(($(nproc)*2))" >> ~/.config/nix/nix.conf

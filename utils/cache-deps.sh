#!/bin/sh
set -eu
nix-store --realise $(nix-store -q --references $(nix-instantiate) | grep -v 'bcdb$') | cachix push bcdb

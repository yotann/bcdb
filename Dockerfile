FROM nixos/nix:2.3.11

# Set up cachix, so dependencies can be downloaded from our cache. (Optional,
# but saves a lot of time rebuilding LLVM with assertions enabled.)
RUN nix-env -iA cachix -f https://cachix.org/api/v1/install && \
    cachix use bcdb && \
    nix-collect-garbage --delete-old && \
    nix-store --optimize

WORKDIR bcdb

# Download all dependencies. This is a separate step so Docker can cache the
# dependencies even across changes of the BCDB source code. We copy only the
# needed files, so we can change other files without invalidating the cache.
# Unfortunately, Docker can't copy multiple directories at once without getting
# their contents mixed up.
COPY nix/bcdb nix/bcdb
COPY nix/cgl nix/cgl
COPY nix/coinutils nix/coinutils
COPY nix/import-flake-lock.nix nix/
COPY nix/nng nix/nng
COPY nix/nngpp nix/nngpp
COPY nix/symphony nix/symphony
COPY .gitignore flake.lock *.nix ./
RUN nix-store --realise $(nix-store -q --references $(nix-instantiate default.nix -A bcdb) | grep -v 'bcdb$') && \
    nix-store --optimize

# Copy the full BCDB source code.
RUN rm -rf *
COPY . ./

# Build, test, and install BCDB.
RUN nix-env -f . -iA bcdb && \
    nix-store --optimize

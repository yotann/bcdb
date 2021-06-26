FROM nixos/nix:2.3.11

# Set up cachix, so dependencies can be downloaded from our cache. (Optional,
# but saves a lot of time rebuilding LLVM with assertions enabled.)
RUN nix-env -iA cachix -f https://cachix.org/api/v1/install && \
    cachix use bcdb && \
    nix-collect-garbage --delete-old && \
    nix-store --optimize

WORKDIR /bcdb

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
COPY nix/symphony nix/symphony
COPY .gitignore flake.lock *.nix ./
RUN set -o pipefail && \
    nix-store --realise $(nix-store -q --references $(nix-instantiate default.nix -A bcdb) | grep -v 'bcdb$') && \
    nix-store --optimize

# Build a small program using the bitcode overlay, just to make sure the
# necessary dependencies are cached by Docker.
COPY nix/bitcode-cc-wrapper nix/bitcode-cc-wrapper
COPY nix/bitcode-overlay nix/bitcode-overlay
RUN nix-build nix/bitcode-overlay -A pkgsBitcode.pv && \
    nix-store --optimize

# Add programs used by nix/gl-experiments scripts.
RUN nix-env -f nix/bitcode-overlay -iA \
    bash \
    llvmPackages_12.llvm \
    python3 \
    python3Packages.matplotlib \
    python3Packages.pandas \
    python3Packages.pyperf && \
    nix-store --optimize

# Copy the full BCDB source code.
RUN rm -rf ./*
COPY . ./

# Enable parallel builds.
RUN echo "max-jobs = 2" >> /etc/nix/nix.conf && \
    echo "cores = 0" >> /etc/nix/nix.conf

# Build, test, and install BCDB.
RUN nix-env -f . -iA bcdb

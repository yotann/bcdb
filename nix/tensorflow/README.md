This is a copy of `pkgs/development/python-modules/tensorflow` from upstream
Nixpkgs. The only change is to allow setting `xlaSupport` without
`cudaSupport` (Nixpkgs thinks this combination is broken, but it seems to work
fine with the current version of TensorFlow). We want this combination because
LLVM needs XLA enabled in order to use TensorFlow, but we want to avoid CUDA
because it's non-free.

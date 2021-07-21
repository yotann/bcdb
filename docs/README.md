# BCDB Projects

The BCDB project has the following subprojects:

- [MemoDB]: A content-addressable store and a memoizing distributed processing
  framework. All the other subprojects are built on MemoDB.
- [BCDB]: The BCDB proper splits LLVM modules into pieces, and uses MemoDB to
  deduplicate and store them.
- [Guided Linking]: A technique for optimizing dynamically linked code as if it
  were statically linked.
- Outlining: An optimization to reduce code size with a more general
  implementation of code outlining.
- [Nix bitcode overlay]: Nix expressions to automatically build lots of Linux
  packages in the form of LLVM bitcode.

[BCDB]: ./BCDB/README.md
[Guided Linking]: ./guided-linking/README.md
[MemoDB]: ./memodb/README.md
[Nix bitcode overlay]: ../nix/bitcode-overlay/README.md

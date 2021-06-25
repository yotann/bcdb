let
  pkgs = import ../bitcode-overlay;
  inherit (pkgs) lib;

  bcdb = (import ../.. {}).bcdb-llvm12;
  llvm = pkgs.llvmPackages_12.llvm;

  populateBCDB = { name, packages, exclude ? [] }: pkgs.runCommand "${name}.bcdb" {
    buildInputs = [ bcdb llvm pkgs.binutils-unwrapped ];
    inherit exclude;
    passAsFile = [ "exclude" ];
  } ''
    hasBC() {
      isELF "$1" || return 1
      objdump -h "$1" | grep -q ".llvmbc"
    }

    export MEMODB_STORE=sqlite:$out
    bcdb init

    while read path; do
      grep -q "$path" "$excludePath" && continue
      echo "Searching for bitcode in $path..."

      # Handle ELF files
      find "$path" -type f | while read elf; do
        if hasBC "$elf"; then
          echo "Extracting bitcode from $elf"
          bc-imitate annotate --binary "$(readlink -e "$elf")" \
              | opt -strip-debug \
              | bcdb add -name "$elf" -
        elif isELF "$elf"; then
          echo "No bitcode found in $elf"
        fi
      done
    done < ${referencesFile packages}

    while read path; do
      grep -q "$path" "$excludePath" && continue
      echo "Searching for symlinks in $path..."

      # Handle symlinks (important for libraries!)
      find "$path" -type l -xtype f | while read link; do
        if hasBC "$link"; then
          elf=$(readlink -e "$link")
          echo "Copying symlink $link -> $elf"
          bcdb cp "$elf" "$link"
        fi
      done
    done < ${referencesFile packages}
  '';

  # Get a file containing a list of all paths in the Nix store referenced
  # (directly or indirectly) by rootPaths.
  referencesFile = rootPaths: "${pkgs.closureInfo { inherit rootPaths; }}/store-paths";

  # List all symbols defined in static libraries, so we can set up the symbol
  # list and force them to be linked in.
  static-symbol-list = pkgs.runCommandLocal "static-symbol-list.txt" {
    buildInputs = [ pkgs.binutils-unwrapped ];
  } ''
    escape() {
      sed -ne 's/[][()^$|*+?.\\{}]/\\\0/g;h;s/.*/fun:\0=/p;g;s/.*/global:\0=/p'
    }

    for arc in ${pkgs.glibc.out}/lib/libc_nonshared.a ${pkgs.gcc-unwrapped}/lib/libstdc++.a; do
      echo "# $arc"
      echo "[gl-always-defined-elsewhere]"
      nm -Agp --defined-only "$arc" | awk '{ print $3 }' | sort | escape
      echo
    done | tee -a $out
  '';

  symlinkJoinReferences = name: packages: pkgs.runCommandLocal "${name}.links" {} ''
    mkdir -p "$out"
    while read path; do
      ${pkgs.xorg.lndir}/bin/lndir -silent "$path" "$out"
    done < ${referencesFile packages}
  '';

  # Remove compiled binaries from a path.
  removeBinaries = symlinks: pkgs.runCommandLocal "${symlinks.name}.clean" {} ''
    cp -r "${symlinks}" "$out"
    chmod -R u+w "$out"
    # Get rid of all binaries.
    find "$out" -xtype f | while read f; do
      if [ -e "$f" ] && isELF "$f"; then
        rm -- "$f"
      elif [[ "$f" =~ /lib/.*.a$ ]]; then
        rm -- "$f"
      fi
    done
    # Get rid of broken links.
    find "$out" -xtype l -print0 | xargs -r -0 rm --
  '';

  # Build the original bitcode using normal LTO.
  buildLTO = name: bcdb-file: flag: pkgs.stdenvNoCC.mkDerivation {
    name = "${name}.lto";
    buildInputs = [ bcdb pkgs.clang_12 pkgs.lld_12 ];
    enableParallelBuilding = true;
    phases = "unpackPhase buildPhase fixupPhase";
    unpackPhase = ''
      src=$PWD
      export MEMODB_STORE="sqlite:${bcdb-file}?immutable=1"

      # Emit Makefile rules to compile every module.
      declare -A ALREADY_HANDLED
      bcdb list-modules | while read mod; do
        elf=$out/''${mod#$NIX_STORE/}
        mkdir -p "$(dirname "$elf")"

        # If we already processed this module under a different name, just make
        # a symlink.
        vid=$(bcdb head-get "$mod")
        if [ ''${ALREADY_HANDLED[$vid]+_} ]; then
          ln -s "''${ALREADY_HANDLED[$vid]}" "$elf"
          continue
        fi
        ALREADY_HANDLED[$vid]="$elf"

        bcdb get -name "$mod" > "$elf.bc"
        IFS=$'\n' args=( $(bc-imitate clang-args "$elf.bc" | sed -e "/^-rpath/!{p;d};s|/nix/store/\([^:]*\)|$out/\1:\0|g") )
        unset IFS
        args+=( -fuse-ld=lld ) # to match merged build
        args+=( -Xlinker -zlazy )

        echo "all: $elf"
        echo
        echo "$elf: $elf.bc"
        printf "\tclang++ -v -xir \"\$<\" -xnone ${flag} -o \"\$@\""
        for arg in "''${args[@]}"; do
          printf " %q" "$arg"
        done
        printf "\n\t@rm \"\$<\"\n\n"
      done > Makefile
    '';

    buildFlags = [ "--keep-going" ];

    preFixup = ''
      find "$out" -type f | while read elf; do
        # Fix rpath to use the newly built libraries.
        if objdump -f "$elf" | egrep -q 'EXEC_P|DYNAMIC'; then
          patchelf --print-rpath "$elf" > rpath
          sed -i -e "s|/nix/store/\([^:]*\)|$out/\1:\0|g" rpath
          patchelf --set-rpath "$(cat rpath)" "$elf"
        fi
      done
    '';
  };

  # Perform guided linking using bcdb gl, producing bitcode.
  gledBitcode = {
    name,
    bcdb-file,
    symbol-list ? "",
    noweak ? true,
    nooverride,
    nouse,
    noplugin,
    make-weak-library ? false
  }: with lib; pkgs.runCommand "${name}.mux" {
    buildInputs = [ bcdb ];
  } ''
    export MEMODB_STORE="sqlite:${bcdb-file}?immutable=1"
    mkdir "$out"
    declare -A ALREADY_HANDLED
    declare -a MODS

    while read mod; do
      vid=$(bcdb head-get "$mod")
      mkdir -p "$(dirname "$out/$mod")"

      # If we already processed this module under a different name, just make
      # a symlink.
      if [ ''${ALREADY_HANDLED[$vid]+_} ]; then
        ln -s "''${ALREADY_HANDLED[$vid]}" "$out/$mod"
        continue
      fi
      ALREADY_HANDLED[$vid]=$out/''${mod#$NIX_STORE/}
      MODS+=("$mod")
    done < <(bcdb list-modules)

    set -x
    bcdb gl -o "$out" \
      --disable-dso-local \
      --gl-symbol-list=${static-symbol-list} \
      --gl-symbol-list=${pkgs.writeText "${name}-symbols.txt" symbol-list} \
      --merged-name="${name}-merged.so" \
      ${optionalString make-weak-library "--weak-name='${name}-weak.so'"} \
      ${optionalString noweak "--noweak"} \
      ${optionalString nooverride "--nooverride"} \
      ${optionalString nouse "--nouse"} \
      ${optionalString noplugin "--noplugin"} \
      "''${MODS[@]}"
    set +x
    for x in "$out/$NIX_STORE"/*; do
      mv "$x" "$out/"
    done
    rmdir "$out/$NIX_STORE"
  '';

  # Compile the bitcode produced by guided linking into executable files.
  gledLTO = name: gl: flag: let
    gledLTOpart = name: gl: flag: pkgs.runCommand "${name}.mux.o" {
      nativeBuildInputs = [ pkgs.clang_12 ];
    } ''
      # Nix will abort the build if we go 2 hours without printing anything.
      # But Nix will also abort the build if we print dozens of MB of log output.
      # So we enable some debugging messages from Clang, but only print every 10000th line.
      merged=${name}-merged.so
      clang++ -xir "${gl}/$merged" -xnone ${flag} -fPIC -c -o $out \
        -v -mllvm -opt-bisect-limit=-1 2>&1 | sed -e '1~10000{p;d};/BISECT/d'
    '';
  in pkgs.stdenvNoCC.mkDerivation {
    name = "${name}.mux";
    nativeBuildInputs = [ bcdb pkgs.clang_12 pkgs.lld_12 ];
    phases = "unpackPhase buildPhase fixupPhase";
    dontStrip = false;

    # patchelf can cause a "maximum file size exceeded" error if the ELF file
    # is huge enough. patchelfUnstable fixes the error, but produces broken ELF
    # files.
    dontPatchELF = true;

    enableParallelBuilding = true;

    unpackPhase = ''
      src=$PWD
      mkdir -p "$out"
      merged=${name}-merged.so
      weak=${name}-weak.so




      if [ -e "${gl}/$weak" ]; then
        # Build the library of weak placeholder definitions.
        set -x
        clang++ \
          -xir "${gl}/$weak" -xnone \
          ${flag} \
          -o "$out/$weak" \
          -fPIC -shared
        set +x

        # Make a chain of libraries depending on the weak library, to ensure that
        # it comes last in the BFS of library definitions.
        for i in {0..15}; do
          clang -shared "$out/$weak" -o "$out/${name}-weak$i.so"
          weak=${name}-weak$i.so
        done
      fi



      obj="${gledLTOpart name gl flag}"
      # Build the merged library.
      args=("$obj" ${flag} -o "$out/$merged")
      IFS=$'\n' args+=( $(bc-imitate clang-args "${gl}/$merged" | sed -e "/^-rpath/!{p;d};s|/nix/store/\([^:]*\)|$out/\1:\0|g") )
      unset IFS

      # Only ld.gold properly supports an external function that takes the address
      # of a protected external function. ld.bfd fails with "relocation
      # R_X86_64_PC32 against protected symbol `foo' can not be used when
      # making a shared object".
      args+=( -fuse-ld=lld )

      # Link against the weak library.
      if [ -e "$out/$weak" ]; then
        args+=( "$out/$weak" )
      fi

      # Not sure whether this is an improvement or not.
      # It can be disabled at runtime with LD_BIND_NOW=1.
      args+=( -Xlinker -zlazy )

      args+=( -Xlinker --no-demangle )

      set -x
      clang++ -v "''${args[@]}"
      set +x



      # Emit Makefile rules to build the stub libraries.
      find "${gl}" -xtype f -mindepth 2 | while read mod; do
        name="$out/''${mod#${gl}/}"
        mkdir -p "$(dirname "$name")"

        if [ -h "$mod" ]; then
          ln -s "$(realpath --relative-to "$(dirname "$mod")" "$mod")" "$name"
          continue
        fi

        IFS=$'\n' args=( $(bc-imitate clang-args "$mod" | sed -e "/^-rpath/!{p;d};s|/nix/store/\([^:]*\)|$out/\1:\0|g") )
        unset IFS
        args+=( -Xlinker --no-demangle )
        args+=( "$out/$merged" )

        args+=( -fuse-ld=lld )
        args+=( -Xlinker -zlazy )

        # 1. The original code has a library that uses a symbol from another library.
        # 2. bcdb gl moves the symbol into a private symbol in the merged library.
        # 3. We link the new program, merged against the original, unmerged
        #    library. This requires --allow-shlib-undefined to work. (Ideally, we
        #    would link against the new library instead.)
        # 4. We change the rpath so that at runtime, the new, merged library is used.
        args+=( -Xlinker --allow-shlib-undefined )

        echo "all: $name"
        echo
        echo "$name: $mod"
        printf "\tclang++ -v -xir \"\$<\" -xnone ${flag} -o \"\$@\""
        for arg in "''${args[@]}"; do
          printf " %q" "$arg"
        done
        printf "\n\n"
      done > Makefile
    '';
  };

  # Merged the compiled executables (which have one subdirectory per package)
  # so the files from all packages are mixed together.
  symlinkLTO = name: original: lto: pkgs.runCommandLocal "${name}.compiled.links" {} ''
    cp -r "${removeBinaries original}" "$out"
    chmod -R u+w "$out"
    for x in "${lto}/${name}"-*.so; do
      [ -e "$x" ] && ln -sf "$x" "$out/"
    done

    sym2dir() {
      local dir=$1
      local real=$(realpath "$dir")
      if [ "$real" != "$dir" ]; then
        sym2dir "$(dirname "$dir")"
        if [ -h "$dir" ]; then
          echo fixing "$dir"
          rm "$dir"
          cp -sa "$real" "$dir"
          chmod -R u+w "$dir"
        fi
      fi
    }

    find "${lto}" -xtype f -mindepth 2 | while read elf; do
      name="$out/''${elf#${lto}/*/}"
      sym2dir "$(dirname "$name")"
      ln -sf "$elf" "$name"
    done
  '';

  default-configurations = {
    closed        = { noplugin = true;  nooverride = true;  nouse = true;  noweak = true;  };
    open-spurious = { noplugin = true;  nooverride = true;  nouse = false; noweak = true;  };
    open          = { noplugin = true;  nooverride = true;  nouse = false; noweak = false; };
    interposable  = { noplugin = true;  nooverride = false; nouse = false; noweak = false; };
    compatible    = { noplugin = false; nooverride = false; nouse = false; noweak = false; };
  };

  makeLinkFarm = name: result: let
    f = path: name: value: { name = lib.concatStringsSep "/" (path ++ [name]); path = value; };
    g = path: name: value: if lib.isDerivation value then f path name value else if lib.isAttrs value then recurse (path ++ [name]) value else [];
    recurse = path: set: lib.mapAttrsToList (g path) set;
    xs = lib.flatten (recurse [] result);
  in pkgs.linkFarm "${name}-links" xs;

  makeExperiment = {
    name,
    packages,
    lto-flags ? "-O3",
    exclude ? [],
    symbol-list ? "",
    configurations ? default-configurations,
    profile-commands ? null
  }: let
    result = rec {
      bcdb-file = populateBCDB { inherit name packages exclude; };
      original = symlinkJoinReferences name packages;
      gl-bitcode = lib.mapAttrs (n: v: gledBitcode (v // { inherit name bcdb-file symbol-list; })) configurations;
      gl-packages = lib.mapAttrs (n: v: gledLTO name v lto-flags) gl-bitcode;
      gl = lib.mapAttrs (n: v: symlinkLTO name original v) gl-packages;
      lto-packages = buildLTO name bcdb-file lto-flags;
      lto = symlinkLTO name original lto-packages;
    };
  in result // { everything = makeLinkFarm name result; recurseForDerivations = true; };

  makeExperimentWithPGO = args @ {
    name,
    lto-flags ? "-O3",
    profile-commands ? null,
    ...
  }: if profile-commands == null then makeExperiment (args // { inherit name; }) else let
    generate-build = makeExperiment (args // { lto-flags = "${lto-flags} -fprofile-generate"; });
    make-profdata = v: pkgs.runCommand "${name}.profraw" {} ''
      export LLVM_PROFILE_FILE=$PWD/profraw
      cd ${v}
      ${profile-commands}
      ${llvm}/bin/llvm-profdata merge -output=$out $LLVM_PROFILE_FILE
    '';
    result = rec {
      inherit generate-build;
      inherit (generate-build) bcdb-file original gl-bitcode;
      lto-profdata = make-profdata generate-build.lto;
      gl-profdata = lib.mapAttrs (n: v: make-profdata v) generate-build.gl;

      gl-packages = lib.mapAttrs (n: v: gledLTO name v "${lto-flags} -fprofile-use=${gl-profdata.${n}}") gl-bitcode;
      gl = lib.mapAttrs (n: v: symlinkLTO name original v) gl-packages;
      lto-packages = buildLTO name bcdb-file "${lto-flags} -fprofile-use=${lto-profdata}";
      lto = symlinkLTO name original lto-packages;
    };
  in result // { everything = makeLinkFarm name result; recurseForDerivations = true; };

  experiment-cfgs = import ./experiments.nix {
    inherit pkgs;
    pkgsBitcode = pkgs.pkgsBitcode;
  };

  experiments = lib.mapAttrs (n: v: makeExperimentWithPGO ({ name = n; } // v)) experiment-cfgs;

in experiments // {
  inherit static-symbol-list;
}

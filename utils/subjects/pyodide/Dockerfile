# pyodide/pyodide:0.18.0, released 2021-08-03
FROM pyodide/pyodide@sha256:d9c4dac987295e12cd434090f5cf825fac8979c4162c8fe742f9f218a103a659

# Remove stuff that was built without LTO.
RUN cd pyodide && rm -rf src/core/*.o cpython/build cpython/installs

# Rebuild everything with LTO.
# Emscripten will automatically rebuild libraries like libc++-noexcept,
# freetype, etc. with LTO enabled, but it will not pass the -g0 -O0 options.
# TODO: force these libraries to use -g0 -O0.
RUN cd pyodide && \
    make build/pyodide.asm.js EXTRA_CFLAGS="-g0 -O0 -flto" EXTRA_LDFLAGS="-g0 -O0 -flto"

# --lto-embed-bitcode=post-merge-pre-opt

# Some libraries are omitted because they can't be built as bitcode:
# - libffi.a
# - libc_rt_wasm.a
# - libcompiler_rt.a
RUN cd pyodide && \
    emsdk/emsdk/upstream/bin/llvm-link -o pyodide-tmp.bc \
      src/core/*.o \
      cpython/installs/python-*/lib/libbz2.a \
      cpython/installs/python-*/lib/libpython*.a \
      cpython/installs/python-*/lib/libsqlite3.a \
      emsdk/emsdk/upstream/emscripten/cache/sysroot/lib/wasm32-emscripten/lto-pic/libc.a \
      emsdk/emsdk/upstream/emscripten/cache/sysroot/lib/wasm32-emscripten/lto-pic/libdlmalloc.a \
      emsdk/emsdk/upstream/emscripten/cache/sysroot/lib/wasm32-emscripten/lto-pic/libsockets.a \
      emsdk/emsdk/upstream/emscripten/cache/sysroot/lib/wasm32-emscripten/lto-pic/libz.a

RUN cd pyodide && \
    emsdk/emsdk/upstream/bin/opt pyodide-tmp.bc -o pyodide.bc \
      --strip-debug \
      --internalize \
      --internalize-public-api-list=main \
      --globalopt

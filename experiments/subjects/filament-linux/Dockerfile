# Ubuntu 20.04 with Clang 13.0, built 2021-10-03.
FROM teeks99/clang-ubuntu@sha256:6eed2b5594e778e2dfeb8e8690992f863db55c9d263bcc872d935d835c4b5523

RUN apt-get update && \
    apt-get install -y \
      curl \
      libgl-dev \
      libxi-dev \
      libxxf86vm-dev \
      ninja-build

# Filament needs newer CMake.
RUN curl -L https://apt.kitware.com/keys/kitware-archive-latest.asc | \
    gpg --dearmor - > /usr/share/keyrings/kitware-archive-keyring.gpg && \
    echo 'deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ focal main' >> /etc/apt/sources.list && \
    apt-get update && \
    apt-get install -y cmake

RUN curl -L https://github.com/google/filament/archive/refs/tags/v1.12.6.tar.gz | tar -xvzf -

COPY bitcode-flags.patch bitcode-flags.patch

RUN cd filament-1.12.6 && \
    sed -i -e 's/env python/env python3/' third_party/spirv-tools/utils/*.py && \
    patch -p1 < ../bitcode-flags.patch && \
    export CC=clang-13 CXX=clang++-13 && \
    ./build.sh release

# emscripten 2.0.26 was the last version based on LLVM pre-13.0.0
# (it should be possible to use newer emscripten with older LLVM, but I don't
# want to figure that out right now).
RUN curl -L https://github.com/emscripten-core/emsdk/archive/2.0.31.tar.gz | tar -xvzf - && \
    cd emsdk-2.0.31 && \
    python3 ./emsdk.py install 2.0.26 && \
    python3 ./emsdk.py activate 2.0.26

# Needed for source command
SHELL ["/usr/bin/bash", "-c"]

RUN source emsdk-2.0.31/emsdk_env.sh && \
    cd filament-1.12.6 && \
    ./build.sh -p webgl release

RUN source emsdk-2.0.31/emsdk_env.sh && \
    cd filament-1.12.6/out/cmake-webgl-release && \
    llvm-link-13 -o /filament-webgl-O0-tmp.bc \
      web/filament-js/CMakeFiles/filament-js.dir/*.o \
      filament/libfilament.a \
      libs/math/libmath.a \
      libs/utils/libutils.a \
      libs/image/libimage.a \
      libs/filameshio/libfilameshio.a \
      libs/gltfio/libgltfio_core.a \
      libs/viewer/libviewer.a \
      third_party/meshoptimizer/libmeshoptimizer.a \
      libs/geometry/libgeometry.a \
      libs/gltfio/libgltfio_resources.a \
      third_party/draco/tnt/libdracodec.a \
      libs/filagui/libfilagui.a \
      filament/backend/libbackend.a \
      libs/filaflat/libfilaflat.a \
      libs/filabridge/libfilabridge.a \
      libs/ibl/libibl-lite.a \
      third_party/imgui/tnt/libimgui.a \
      third_party/civetweb/tnt/libcivetweb.a \
      $EMSDK/upstream/emscripten/cache/sysroot/lib/wasm32-emscripten/lto/libGL-webgl2.a \
      $EMSDK/upstream/emscripten/cache/sysroot/lib/wasm32-emscripten/lto/libc++-noexcept.a \
      $EMSDK/upstream/emscripten/cache/sysroot/lib/wasm32-emscripten/lto/libc++abi-noexcept.a \
      $EMSDK/upstream/emscripten/cache/sysroot/lib/wasm32-emscripten/lto/libc.a \
      $EMSDK/upstream/emscripten/cache/sysroot/lib/wasm32-emscripten/lto/libdlmalloc.a \
      $EMSDK/upstream/emscripten/cache/sysroot/lib/wasm32-emscripten/lto/libsockets.a

# TODO: not sure if this is exporting the right things
RUN opt-13 /filament-webgl-O0-tmp.bc -o /filament-webgl-O0.bc \
      --strip-debug \
      --internalize \
      --internalize-public-api-list=main,emscripten_stack_get_end,emscripten_stack_get_free,emscripten_stack_init,stackSave,stackRestore,stackAlloc,__wasm_call_ctors,fflush,__errno_location,malloc,free,emscripten_main_thread_process_queued_calls,ntohs,__dl_seterr,htonl,htons,_get_tzname,_get_daylight,_get_timezone,sysconf,__start_em_asm,__stop_em_asm \
      --globalopt

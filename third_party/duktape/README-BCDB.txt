This directory contains Duktape version 2.6.0 configured with:

python tools/configure.py -DDUK_USE_CPP_EXCEPTIONS -DDUK_USE_FATAL_HANDLER -DDUK_USE_PANIC_HANDLER --fixup-line "#ifndef NDEBUG" --fixup-line "#define DUK_USE_ASSERTIONS" --fixup-line "#endif"

The duktape.c file has been renamed to duktape.cpp, and the CMakeLists.txt file
has been added.

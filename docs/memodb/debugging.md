# Debugging MemoDB errors

<!-- markdownlint-disable MD014 -->

## Types of errors

- User error. If you try something on a small, easy input and it breaks, you
  probably did something wrong.
- BCDB bugs. If something works on small inputs, but you get an error on large
  inputs, there's probably a bug in BCDB.
- LLVM bugs. These are less common than the other two, but they do happen.

## Hangs

If the BCDB commands seem to hang without printing anything, there are several
possible reasons:

- If the command hangs right after you start it, it might be rewriting files in
  the RocksDB database. This should normally take less than a minute. In this
  case, the `top` command will show that BCDB is using CPU.
- It's still working on a task, but the task is taking a really long time. For
  example, maybe `smout.candidates` is working on a really huge function and
  generating 10,000 candidates. In this case, the `top` command will show that
  BCDB is using CPU.
- There's a bug in BCDB and it's stuck in an infinite loop. In this case, the
  `top` command will show that BCDB is using CPU. You can try to investigate by
  using `gdb` to attach to it.
- BCDB already finished all the jobs and printed the results, but it didn't
  exit properly. In this case, BCDB will have printed results, and the `top`
  command will show very low CPU usage. You can just kill the command if this
  happens.
- If you're using `memodb-server` and one of the worker programs or other
  programs crashed or got killed: most likely `memodb-server` sent a job to the
  worker, but it never got a result, so it's waiting forever. In this case, the
  `top` command will show very low CPU usage, but `memodb-server` will still
  respond to any requests you make with `curl`. You can fix this by restarting
  `memodb-server` and all the other programs connected to it.
- If you're using `memodb-server` and nothing has crashed or been killed: you
  might have run into a bug in NNG (the HTTP server library). In this case, the
  `top` command will show very low CPU usage, and `memodb-server` will **not**
  respond to any requests you make with `curl`. Unfortunately, I don't know any
  easy way to fix this problem. I guess you would have to rewrite
  `tools/memodb-server/memodb-server.cpp` to use a different library instead of
  NNG.

## Read the whole error message

It's important to look at **all parts** of the error message to find the source
of the error. For example, consider this error message:

<!-- markdownlint-disable MD013 -->

```text
LLVM ERROR: Incorrect number of arguments for test.add
 #0 0x00007f581fae535d llvm::sys::PrintStackTrace(llvm::raw_ostream&, int) (///nix/store/iqm3zq1acrf4g922rgldc6h8jr9vl9cr-llvm-12.0.0-lib/lib/libLLVM-12.so+0xd8e35d)
 #1 0x00007f581fae35f4 llvm::sys::RunSignalHandlers() (///nix/store/iqm3zq1acrf4g922rgldc6h8jr9vl9cr-llvm-12.0.0-lib/lib/libLLVM-12.so+0xd8c5f4)
 #2 0x00007f581fae377e SignalHandler(int) (///nix/store/iqm3zq1acrf4g922rgldc6h8jr9vl9cr-llvm-12.0.0-lib/lib/libLLVM-12.so+0xd8c77e)
 #3 0x00007f5825da8700 __restore_rt (/nix/store/sbbifs2ykc05inws26203h0xwcadnf0l-glibc-2.32-46/lib/libpthread.so.0+0x13700)
 #4 0x00007f581e7a033a raise (/nix/store/sbbifs2ykc05inws26203h0xwcadnf0l-glibc-2.32-46/lib/libc.so.6+0x3c33a)
 #5 0x00007f581e78a523 abort (/nix/store/sbbifs2ykc05inws26203h0xwcadnf0l-glibc-2.32-46/lib/libc.so.6+0x26523)
 #6 0x00007f581fa27daa llvm::report_fatal_error(llvm::Twine const&, bool) (///nix/store/iqm3zq1acrf4g922rgldc6h8jr9vl9cr-llvm-12.0.0-lib/lib/libLLVM-12.so+0xcd0daa)
 #7 0x00000000004c4d6e (memodb+0x4c4d6e)
 #8 0x00000000004c4b5c std::_Function_handler<memodb::NodeOrCID (memodb::Evaluator&, memodb::Call const&), std::function<memodb::NodeOrCID (memodb::Evaluator&, memodb::Call const&)> memodb::Evaluator::funcImpl<memodb::NodeRef, memodb::NodeRef, 0ul, 1ul>(llvm::StringRef, memodb::NodeOrCID (*)(memodb::Evaluator&, memodb::NodeRef, memodb::NodeRef), std::integer_sequence<unsigned long, 0ul, 1ul>)::'lambda'(memodb::Evaluator&, memodb::Call const&)>::_M_invoke(std::_Any_data const&, memodb::Evaluator&, memodb::Call const&) /nix/store/h3f8rn6wwanph9m3rc1gl0lldbr57w3l-gcc-10.3.0/include/c++/10.3.0/bits/std_function.h:291:2
 #9 0x00000000004c5451 std::function<memodb::NodeOrCID (memodb::Evaluator&, memodb::Call const&)>::operator()(memodb::Evaluator&, memodb::Call const&) const /nix/store/h3f8rn6wwanph9m3rc1gl0lldbr57w3l-gcc-10.3.0/include/c++/10.3.0/bits/std_function.h:0:14
#10 0x00000000004c5451 (anonymous namespace)::ThreadPoolEvaluator::evaluate(memodb::Call const&) /home/sean/p/bcdb/lib/memodb/Evaluator.cpp:127:37
#11 0x00000000004b600d Evaluate() /home/sean/p/bcdb/tools/memodb/memodb.cpp:319:3
#12 0x00000000004b600d main /home/sean/p/bcdb/tools/memodb/memodb.cpp:490:12
#13 0x00007f581e78bded __libc_start_main (/nix/store/sbbifs2ykc05inws26203h0xwcadnf0l-glibc-2.32-46/lib/libc.so.6+0x27ded)
#14 0x00000000004b54ba _start /build/glibc-2.32/csu/../sysdeps/x86_64/start.S:122:0

Fatal error! This is probably either a bug in BCDB, or you are using it incorrectly.
When you share this error, please include all parts of the error message.
See docs/memodb/debugging.md for debugging suggestions.

Stack dump:
0.      Program arguments: memodb evaluate /call/test.add/uAXEAAfY
1.      ...which was evaluating /call/test.add/uAXEAAfY
            to try again, run:
              memodb evaluate /call/test.add/uAXEAAfY
            to save the inputs that caused the failure, run:
              memodb export -o fail.car /cid/uAXEAAfY
```

<!-- markdownlint-enable MD013 -->

This message actually has four parts:

- The `LLVM ERROR` line, which gives you the main explanation of the error. The
  `LLVM ERROR` line is very clear in this error message, but sometimes it's
  missing.
- The backtrace, which shows you where in the code the error happened. In this
  case you can see that `ThreadPoolEvaluator::evaluate` was trying to call a
  template function, which in turn called `llvm::report_fatal_error`.
- The generic "Fatal error!" message.
- The pretty stack trace, which shows you what arguments the program was
  started with. If the error happens while evaluating a func, it will also show
  you which func it was. In this case, it happened while evaluating
  `/call/test.add/uAXEAAfy`, and you can easily see that this call has only one
  argument instead of two.

## Reproducing errors

You can usually reproduce the error by running the same command again. If the
error happened while evaluating a call, you can reproduce it faster by
evaluating just that particular call. For example, with the error message
above, you would run `memodb evaluate /call/test.add/uAXEAAfY` (from the
third-to-last line).

## Sending error inputs to other people

If the error happens while evaluating a call, you can send the call inputs to
someone else so they can also reproduce the error. For example, you would do:

```console
$ memodb export -o fail.car /cid/uAXEAAfY
exporting /cid/uAXEAAfY
Exported with Root CID: uAXGg5AIgta-laOMSPnk5crcWCR1nDqZH1MXNvNwWXhCda3L_9gA
$ # send the fail.car file to another person or another machine
$ # also send them the "memodb evaluate ..." command that triggers the error
```

The other person would do:

```console
$ export MEMODB_STORE=sqlite:$HOME/memodb-fail.db
$ memodb init
$ memodb transfer --store=car:fail.car --target-store=$MEMODB_STORE
$ # now the store contains the Nodes we need to evaluate the call,
$ # so we can reproduce the error
$ memodb evaluate /call/test.add/uAXEAAfY
```

## Getting backtraces with GDB

Usually the BCDB programs will automatically print a backtrace when they crash.
Sometimes this doesn't work, and you might have to run `gdb --args memodb
evaluate /call/test.add/uAXEAAfY` and then type `run` to start the program in
GDB. Then, after it crashes, you can run `bt` to see the backtrace.

If there are multiple threads involved, you can run `info threads` to see a
list of all threads, `thread <number>` to switch to a specific thread, and `bt`
to see the backtrace for that thread.

## Memory corruption errors

If you think memory is being corrupted, you can run BCDB programs with memory
checking by using either Valgrind or AddressSanitizer. If you build BCDB
yourself, there's an easy way to enable AddressSanitizer:

```console
$ mkdir build
$ cd build
$ cmake .. -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=SANITIZE -DCMAKE_INSTALL_LIBDIR=/var/empty/install
$ make -j10
```

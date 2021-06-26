# Using Bugpoint with BCDB

You can use [Bugpoint](https://llvm.org/docs/Bugpoint.html) to reduce test
cases that crash BCDB. First you need a custom compile script. For example, if
you have a crash in `bcdb merge`:

```bash
#!/bin/sh
set -e
bin/bcdb add   "$@" -name test
bin/bcdb merge test > /dev/null
```

If this script is in `try-merge.sh`, you can run bugpoint like so:

```shell
export MEMODB_STORE=sqlite:bugpoint.bcdb
bin/memodb init
bugpoint -verbose-errors -disable-attribute-remove -compile-custom -compile-command=./try-merge.sh failing-test.bc
opt -globalopt -S < bugpoint-reduced-simplified.bc
```

The `-verbose-errors` option prints the output of every crashing run, to help
you check whether all the crashes have the same root cause. The
`-disable-attribute-remove` option tells Bugpoint to skip removing function
attributes, which is very slow and not usually important. The final
`-globalopt` pass removes unused global declarations, which usually aren't
needed to cause the crash.

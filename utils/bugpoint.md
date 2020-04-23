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
$ export BCDB_URI=sqlite:bugpoint.bcdb
$ bin/bcdb init
$ bugpoint -compile-custom -compile-command=./try-merge.sh failing-test.bc
$ opt -globalopt -S < bugpoint-reduced-simplified.bc
```

# Using Bugpoint with BCDB

You can use [Bugpoint](https://llvm.org/docs/Bugpoint.html) to reduce test
cases that crash BCDB. First you need a custom compile script. For example, if
you have a crash in `bcdb merge`:

```bash
#!/bin/sh
set -e
bin/bcdb add   -uri sqlite:bugpoint.bcdb "$@" -name test
bin/bcdb merge -uri sqlite:bugpoint.bcdb test > /dev/null
```

If this script is in `try-merge.sh`, you can run bugpoint like so:

```shell
$ bin/bcdb init -uri sqlite:bugpoint.bcdb
$ bugpoint -compile-custom -compile-command=./custom.sh failing-test.bc
$ opt -globalopt -S < bugpoint-reduced-simplified.bc
```

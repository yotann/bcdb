# Semantic Outlining experiment scripts

## Usage

### Install software

Nix must be installed. (It's used at the beginning of `smout-runner.py` to
install Python libraries.)

LLVM 13, BCDB, the modified Alive2, and GNU parallel must be installed in your
PATH.

### Start MemoDB server

The store should go on an SSD with several GB free space.

```shell
MEMODB_STORE=rocksdb:store.bcdb memodb init
ulimit -n65536
MEMODB_STORE=rocksdb:store.bcdb memodb-server http://127.0.0.1:29179
```

### Start Alive workers

You should start one process per core, and restart the processes when they
crash. The following command will work:

```shell
yes | parallel -u -n0 alive-worker http://127.0.0.1:29179
```

### Start Smout worker

You only need one process because it's multithreaded.

```shell
MEMODB_STORE=http://127.0.0.1:29179 smout worker -j=all
```

### Choose bitcode file

Edit `config/config.yaml` and set `files.input_bc` to the path to the input
bitcode. It's a good idea to try `ppmtomitsu.bc` first because it's so small.

### Run experiments

```shell
./run-all.sh
```

You should see steady output from the `smout worker` process, and possibly from
the other processes.

If nothing is being printed (except "still waiting"/"job in progress"
messages) and CPU usage is low, something may be stuck. Kill all the processes
and restart everything and it should resume where it left off. When you restart
`memodb-server` it may take a minute before it prints "Server started!", as it
needs to restore data from the database log files.

When everything is finished, the `run-all.sh` command will print an `All
Experiments Result:` line. This line is also saved to `runner.log`.

### Analyze results

You can get a summary of results using the `All Experiments Result:` CID like
this:

```shell
./analyze.py /cid/uAXGg5AIgR1HXvTIpowsCgqO42tTLnJQOZYRomCcnwozmT7Qh2IA
```

You can also use the `memodb` command to examine the results:

```shell
memodb get /cid/uAXGg5AIgR1HXvTIpowsCgqO42tTLnJQOZYRomCcnwozmT7Qh2IA
```

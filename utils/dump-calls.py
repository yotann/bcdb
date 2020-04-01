#!/usr/bin/env python3

# This script opens a BCDB file directly and prints out all the cached results
# for a given function.

import cbor2
import pprint
import sqlite3
import sys

from bcdb import *

if len(sys.argv) != 3:
    print(f'Usage: {sys.argv[0]} path/to/database name_of_function')

bcdb = BCDB(sys.argv[1])
function_name = sys.argv[2]

# Look up the function ID based on its name.
bcdb.cursor.execute('SELECT fid FROM func WHERE name = ?', (function_name,))
fid, = bcdb.cursor.fetchone()

# Function call results are stored in the "call" table. There is a separate row
# for each argument, so if we stored the call f(101,102,103)=200, the table
# rows might look like this:
#
# - cid=0 fid=50 parent=NULL arg=101 result=NULL
# - cid=1 fid=50 parent=0    arg=102 result=NULL
# - cid=2 fid=50 parent=1    arg=103 result=200

parent_queue = [(None, [])] # queue of (parent's cid, parent's args up to this point)
while parent_queue:
    parent, parent_args = parent_queue.pop()
    bcdb.cursor.execute('SELECT cid, arg, result FROM call WHERE fid=? AND parent IS ?', (fid, parent))
    for cid, arg, result in bcdb.cursor.fetchall():
        args = parent_args + [Ref(str(arg))]
        parent_queue.append((cid, args))
        if result is not None:
            result = bcdb.get(Ref(str(result)))
            print(f'{function_name}({", ".join(str(arg) for arg in args)}) = {pprint.pformat(result)}')

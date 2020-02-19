#!/usr/bin/env python3

# This script opens a BCDB file directly and prints out all the cached results
# for a given function.

import cbor2
import pprint
import sqlite3
import sys

if len(sys.argv) != 3:
    print(f'Usage: {sys.argv[0]} path/to/database name_of_function')

function_name = sys.argv[2]
conn = sqlite3.connect(sys.argv[1])
cursor = conn.cursor()

# A reference to one of the values in the BCDB.
class Ref:
    def __init__(self, ref):
        self.ref = ref
    def __str__(self):
        return f'Ref({self.ref})'
    def __repr__(self):
        return f'Ref({self.ref})'

# Load a value. All values are stored in the "blob" table.
def load_value(ref):
    cursor.execute('SELECT type, content FROM blob WHERE vid=?', (int(ref.ref),))
    typ, content = cursor.fetchone()
    if typ == 0:
        # Binary data stored directly.
        return content
    elif typ == 2:
        # Value stored as CBOR.
        # References to other values are stored as strings with CBOR tag 39.
        def tag_hook(decoder, tag, shareable_index=None):
            if tag.tag == 39:
                return Ref(tag.value)
            return tag
        return cbor2.loads(content, tag_hook=tag_hook)
    else:
        assert False, f'Unsupported value type {typ}'

# Look up the function ID based on its name.
cursor.execute('SELECT fid FROM func WHERE name = ?', (function_name,))
fid, = cursor.fetchone()

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
    cursor.execute('SELECT cid, arg, result FROM call WHERE fid=? AND parent IS ?', (fid, parent))
    for cid, arg, result in cursor.fetchall():
        args = parent_args + [Ref(str(arg))]
        parent_queue.append((cid, args))
        if result is not None:
            result = load_value(Ref(str(result)))
            print(f'{function_name}({", ".join(str(arg) for arg in args)}) = {pprint.pformat(result)}')

#!/usr/bin/env python3

# This module can be used to read BCDB instances directly from Python.

import cbor
import sqlite3

# A reference to one of the values in the BCDB.
class Ref:
    def __init__(self, ref):
        self.ref = ref
    def __str__(self):
        return f'Ref({self.ref})'
    def __repr__(self):
        return f'Ref({self.ref})'

class BCDB:
    tag_mapper = cbor.TagMapper([cbor.ClassTag(39, Ref, lambda ref: ref.ref, Ref)])

    def __init__(self, filename):
        self.filename = filename
        self.conn = sqlite3.connect(filename)
        self.cursor = self.conn.cursor()

        self.cursor.execute('PRAGMA user_version')
        user_version, = self.cursor.fetchone()
        if user_version != 3:
            assert False, f'Unsupported BCDB version (open it with bin/bcdb to upgrade)'

    def get_heads(self):
        self.cursor.execute('SELECT name, vid FROM head')
        return {head: Ref(str(head_vid)) for head, head_vid in self.cursor.fetchall()}

    # Load a value. All values are stored in the "blob" table.
    def get(self, ref):
        self.cursor.execute('SELECT type, content FROM blob WHERE vid=?', (int(ref.ref),))
        typ, content = self.cursor.fetchone()
        if typ == 0:
            # Binary data stored directly.
            return content
        elif typ == 2:
            # Value stored as CBOR.
            # References to other values are stored as strings with CBOR tag 39.
            return BCDB.tag_mapper.loads(content)
        else:
            assert False, f'Unsupported value type {typ}'

    def get_blob_size(self, ref):
        self.cursor.execute('SELECT LENGTH(content) FROM blob WHERE vid = ?', (int(ref.ref),))
        blob_size, = self.cursor.fetchone()
        return blob_size

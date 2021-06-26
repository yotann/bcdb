# The MemoDB data model

<!-- markdownlint-disable MD001 MD024 -->

## Overview

MemoDB's content-addressable stores can maintain three types of data:

- A **Node**, consisting of arbitrary structured data. Each node is uniquely
  identified by its **CID**.
- A **Head**, associating an arbitrary Node with a user-friendly name.
- A **Call**, which caches the result of calling a named function on a given
  list of Nodes.

This document explains each type in more detail.

## Node

A Node consists of arbitrary structured data. Nodes support a subset of the
[IPLD Data Model]—specifically, the subset implemented by [DAG-CBOR]. Each Node
can hold one of the following kinds of data:

- The null value.
- A boolean true or false.
- An integer in the signed 64-bit integer range (from -9223372036854775808 to
  9223372036854775807, inclusive).
- A float in the double-precision floating point range, but excluding
  infinities and NaNs.
- A text string, which must consist of valid Unicode codepoints.
- A byte string, containing arbitrary bytes.
- A link, which consists of a single CID representing a reference to another
  Node that is stored separately.
- A lists containing an arbitrary ordered sequence of other Nodes. Nodes in the
  same list may have different kinds.
- A map, containing a mapping from text string keys to arbitrary other values.
  Duplicate keys are prohibited, and ordering of map elements is insignificant.

Note that lists and maps contain other Node values directly, whereas a link is
an indirect reference to another Node.

Because floats are usually computed using approximations, two programs
attempting to perform the same calculation may get different results. This will
interfere with MemoDB's deduplication process. Therefore, floats should be
avoided when there are good alternatives available.

Nodes are usually represented in [DAG-CBOR], which is a binary format. They can
also be represented in [MemoDB JSON], which is a textual format.

MemoDB does not enforce any particular schema for Nodes; they can use arbitrary
combinations of different kinds of data. Clients may wish to enforce schemas
themselves, perhaps using [CDDL] or [IPLD Schemas].

#### Rationale

##### CBOR

It was desirable to use a serialization format that has wide support across
different programming languages. JSON, YAML, and TOML were rejected because
they can't support binary data efficiently. BSON and MessagePack were rejected
because they limit byte strings to 4 GB, which may be insufficient for
extremely large bitcode modules and other data, and are somewhat irregular and
difficult to extend in compatible ways. [CBOR] was chosen because it
efficiently supports all kinds of data including large binary data, uses a
regular encoding, and is extensible through the [CBOR Tag Registry].

##### DAG-CBOR and limitations on CBOR values

By limiting Nodes to the values supported by [DAG-CBOR], we ensure that they
can be processed correctly by IPLD tools such as the `ipfs` program. However,
no practical use has been found for doing so, and this decision may be
revisited in the future. (`ipfs` is fairly inefficient at handling huge numbers
of small nodes, so it isn't very suitable for use with MemoDB.)

[CBOR] supports an extremely general data model, and [DAG-CBOR] places many
restrictions on it:

- The undefined value is not supported. This is not expected to be a problem.
- Infinities and NaNs are not supported. This is not expected to be a problem.
- Tags are not supported, except for the CID tag. Some tags, like timestamps,
  packed integer arrays, and bignums, could be useful in MemoDB. On the other
  hand, the more tags we support, the fewer CBOR implementations will be
  compatible with MemoDB Nodes.
- Map keys may only be text strings. It would be nice to support byte strings
  and integers as well; for example, LLVM symbol names are arbitrary sequences
  of bytes, and are used as keys. On the other hand, some highly useful CBOR
  libraries like [jsoncons] have limited support for keys other than text
  strings. For now, byte strings are converted to text strings by treating them
  as ISO-8859-1 and decoding them into Unicode, but this decision should be
  revisited in the future.

Note that arbitrary CBOR values, ignoring the above restrictions, can be
encoded as byte strings and then stored in a MemoDB Node. However, such values
may not use links or CIDs to refer to other Nodes, because MemoDB will not
recognize such links or CIDs, and might delete the other Nodes linked to.

## CID

A CID is a unique identifier for a Node, following IPLD's [CID specification].
A given Node will always have the same CID across different MemoDB stores,
computer architectures, etc., except when changes are made to the MemoDB
design. If the same Node is added multiple times to one MemoDB store, the store
will generally deduplicate it by using the Node's CID to detect that it has
already been stored.

Each CID has multiple parts: the multibase, CID version, content type, and
multihash. MemoDB places limits on each of these parts.

### Multibase

The multibase indicates which encoding is used for the rest of the CID.

In binary formats, the CID should be stored using its binary encoding.
Depending on the format, the `0x00` multibase prefix may or may not be present;
the [DAG-CBOR] format requires it.

In plaintext, such as the output of command-line tools, the CID is normally
printed using the `base32` multibase, which has a prefix of `b`; note that this
multibase uses base32 with lowercase letters and omits padding. In [MemoDB
JSON], CIDs are written using the `base64pad` multibase, which has a prefix of
`M`. Other multibases may also be supported.

#### Rationale

Outside the MemoDB core, CIDs will usually be treated as opaque byte strings or
text strings, so the exact encoding isn't important. However, it's occasionally
useful to use other bases (such as `base16` when copying a CID from a hex
dump).

Base32 has several advantages that justify making it the default for textual
CIDs:

- It is more efficient than base16 and lower bases.
- It avoids punctuation, unlike base64 and higher bases, making it easier to
  select in terminals and reducing the amount of escaping necessary when
  wrapping it in other formats, like filenames, URIs, and command-line
  arguments.
- It is simpler to encode and decode than non-power-of-two bases, like base58.
- It is the default for CIDv1 in `ipfs`.

### CID version

MemoDB uses CIDv1, which is the latest version as of this writing.

### Content type

MemoDB supports two content types:

- `raw` (code 0x55), for Nodes consisting of a single byte string.
- `dag-cbor` (code 0x71), for all other Nodes.

MemoDB uses `raw` for all Nodes consisting of a single byte string, and
`dag-cbor` only for Nodes that are not byte strings.

#### Rationale

The `raw` content type is slightly more efficient for Nodes consisting of plain
byte strings, which are very common in MemoDB. Both these content types are
supported by the `ipfs` program.

### Multihash

MemoDB supports only two multihashes:

- `identity` (code 0x00), which includes the Node data directly in the CID and
  does not require separate storage.
- `blake2b-256` (code 0xb220), which includes only the Blake2b 256-bit hash of
  the Node data in the CID. The actual Node data must be stored separately by
  the MemoDB store.

MemoDB uses the `identity` CID for a Node whenever it would be the same length
or shorter than the `blake2b-256` CID.

#### Rationale

Although `ipfs` uses SHA2-256 as its default, Blake2b-256 is believed to be
more secure, and it is also significantly faster when special hardware support
for SHA2 is not available. It is supported by `ipfs`, and is the default hash
implemented by [libsodium].

The `identity` multihash is much more efficient for small values because it
avoids a level of indirection. For example, the result of the `refines` Call
may be a simple boolean value. This value will be represented with an
`identity` CID, allowing the result to be decoded without actually loading
another Node from the MemoDB Store.

## Head

A Head is simply a user-friendly way of assigning an arbitrary textual name to
a Node. A single Node may have an arbitrary number of names assigned to it; a
single name may be assigned to at most one Node.

## Call

A Call caches the result of applying a function (identified by a textual name)
to zero or more Nodes as arguments. When the same function is called on the
same arguments in the future, MemoDB can look up the cached result instead of
recalculating it.

For best results, the function should be a pure, deterministic function of its
arguments. For example, if a function applies an LLVM pass, all command-line
options that affect that pass should be included in one of the arguments to the
Call. However, determinism and purity are not enforced.

If the cached Calls for a given function are no longer valid (for example,
because the function has been modified), the MemoDB store makes it possible to
delete all such Calls at once.

MemoDB store implementations are optimized for Calls with a small number of
arguments (up to 4 or so). If more arguments are desired, consider combining
multiple arguments into a single map or list Node.

## References and garbage collection

MemoDB stores can keep track of which Nodes link to which other Nodes. This is
useful for debugging; the `memodb refs-to` command can be used recursively to
find which Heads and Calls refer (directly or indirectly) to a given Node.

Note that store implementations do not necessarily track every link. As of this
writing, the `car` store doesn't track links at all, and the `rocksdb` store
tracks most links but not links to Nodes that use the `identity` multihash.
Only the `sqlite` store tracks every link.

When data is copied from one MemoDB store to another using `memodb transfer`,
the only Nodes copied by default are the ones reachable from a Head or Call.
Other Nodes are considered to be garbage, and will be ignored. In the future,
MemoDB may also support garbage collection on a single store.

[CBOR]: https://cbor.io/
[CBOR Tag Registry]: https://www.iana.org/assignments/cbor-tags/cbor-tags.xhtml
[CDDL]: https://tools.ietf.org/html/rfc8610
[CID specification]: https://github.com/multiformats/cid
[DAG-CBOR]: https://github.com/ipld/specs/blob/master/block-layer/codecs/dag-cbor.md
[IPLD Data Model]: https://github.com/ipld/specs/blob/master/data-model-layer/data-model.md
[IPLD Schemas]: https://github.com/ipld/specs/tree/master/schemas
[jsoncons]: https://github.com/danielaparker/jsoncons
[libsodium]: https://doc.libsodium.org/
[MemoDB JSON]: ./json.md
[multibase]: https://github.com/multiformats/multibase

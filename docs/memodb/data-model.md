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
[CBOR] data model. Each Node can hold one of the following kinds of data:

- The null value.
- A boolean true or false.
- An integer in the signed 64-bit integer range (from -9223372036854775808 to
  9223372036854775807, inclusive).
- A float in the double-precision floating point range, including infinities
  and NaNs.
- A text string, which must consist of valid Unicode codepoints.
- A byte string, containing arbitrary bytes.
- A link, which consists of a single CID representing a reference to another
  Node that is stored separately.
- A list, containing an arbitrary ordered sequence of other Nodes. Nodes in the
  same list may have different kinds.
- A map, containing a mapping from text string keys to arbitrary other values.
  Duplicate keys are prohibited, and ordering of map elements is insignificant.

Note that lists and maps contain other Node values directly, whereas a link is
an indirect reference to another Node.

Because floats are usually computed using approximations, two programs
attempting to perform the same calculation may get different results. This will
interfere with MemoDB's deduplication process. Therefore, floats should be
avoided when there are good alternatives available.

Nodes are usually represented in [CBOR], which is a binary format. Nodes can
also be represented in [MemoDB JSON], which is a textual format.

MemoDB does not enforce any particular schema for Nodes; they can use arbitrary
combinations of different kinds of data. Clients may wish to enforce schemas
themselves, perhaps using [CDDL] or [IPLD Schemas].

### CBOR encoding

Encoding follows the [CBOR] standard, with the following restrictions:

- Except for floating-point values, nodes are encoded with [CBOR Core
  Deterministic Encoding].
- Floating-point values are always encoded in 64-bit form.
- Links are encoded using [CIDs in CBOR], and always include the identity
  Multibase prefix (null byte).
- The only supported tag is the CID tag (42).
- The only supported simple values are true, false, and null.

In general, the MemoDB server and tools will accept CBOR that doesn't follow
these restrictions, and will convert Nodes to follow the restrictions before
storing them.

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

##### Limitations on CBOR values

CBOR supports an extremely general data model. For the sake of compatibility
and simplicity, we restrict it to the values supported by [DAG-CBOR], plus
certain kinds of data that have proven useful in MemoDB. The following values
have been added to those supported by [DAG-CBOR]:

- Floating point infinities and NaNs.

The following values supported by CBOR have been excluded from MemoDB Nodes:

- The undefined value.
- Integers outside the signed 64-bit range. Most JSON implementations for C/C++
  only support the unsigned and signed 64-bit ranges.
- Tags, except for the CID tag. Some tags, like timestamps, packed integer
  arrays, and bignums, could be useful in MemoDB. On the other hand, the more
  tags we support, the fewer CBOR implementations will be compatible with
  MemoDB Nodes.
- Map keys may only be text strings. It would be nice to support byte strings
  and integers as well; for example, the outliner represents candidate groups
  using byte strings and uses them as keys. On the other hand, some highly
  useful CBOR libraries like [jsoncons] have limited support for keys other
  than text strings. For now, byte strings are encoded in base64 before being
  used as keys, but this decision should be revisited in the future.

##### CBOR encoding restrictions

The restrictions have been chosen so that Nodes containing
[DAG-CBOR]-compatible values will be encoded with [DAG-CBOR]. Such Nodes can be
processed correctly by IPLD tools such as the `ipfs` program. (However, `ipfs`
is fairly inefficient at handling huge numbers of small nodes, so it isn't very
suitable for use with MemoDB.)

Note that maps are encoded using the new key ordering given in RFC 8949
(bytewise lexicographical order). [DAG-CBOR] uses the old key ordering from RFC
7049 (by length, then bytewise lexicographical); however, the orderings are
actually equivalent for canonically encoded text string keys, which are the
only kind of key supported by [DAG-CBOR].

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
printed using the `base64url` multibase, which has a prefix of `u`; note that
this multibase omits padding. This multibase is also the only one supported for
CIDs in [MemoDB JSON]. In URLs and command-line arguments, the following
multibases are supported: `base16`, `base16upper`, `base32`, `base32upper`,
`base64`, `base64pad`, `base64url`, and `base64urlpad`.

#### Rationale

- `base16` is supported because it can be copied from or compared against hex
  dumps.
- `base32` is supported because it is the default for CIDv1 in `ipfs`, and it
  is more efficient than `base16` without needing any punctuation characters
  and without being case-sensitive.
- `base16upper` and `base32upper` are supported so that `base16` and `base32`
  CIDs still work after being converted to uppercase.
- `base64pad` is supported because it is the most widely supported
  binary-to-text encoding. Note that some useful implementations, like Python's
  `base64` module and the Busybox/Coreutils `base64` command, require padding.
- `base64url` is supported because it is one of the most efficient multibases,
  and it doesn't need to be escaped in URLs or command-line arguments. (The
  first character is always `u`, so it can't be confused for a command-line
  option.)
- `base64` and `base64urlpad` are supported to round out the variations of
  Base64.
- `identity` is NOT supported in text formats because it's byte-based, not
  text-based.
- `base58btc` and `base58flickr` are NOT supported because they are more
  complicated to encode and decode, and offer limited advantages over `base32`
  and `base64url`.
- `base32hex` and `base32pad` and their variants are NOT supported because they
  offer limited advantages over `base32`.
- Other Multibases are NOT supported because they are niche formats with no
  clear use in the MemoDB context.

Outside the MemoDB core, CIDs will usually be treated as opaque byte strings or
text strings, so being able to decode the multibase isn't usually important.
The default multibase is `base64url` because:

- It is the most efficient Multibase (tied with `base64`).
- It doesn't require escaping when used in URLs or command-line arguments.
- If necessary, it can be decoded with a standard Base64 decoder after
  adding padding and replacing `-` and `_` with `+` and `/`.

### CID version

MemoDB uses CIDv1, which is the latest version as of this writing.

### Content type

MemoDB supports three content types:

- `raw` (code 0x55), for Nodes consisting of a single byte string.
- `dag-cbor` (code 0x71), for Nodes that contain links and comply with the
  [DAG-CBOR] restrictions.
- `dag-cbor-unrestricted` (code 0x0171), for Nodes that contain links and do
  not comply with the [DAG-CBOR] restrictions.

#### Rationale

The `raw` content type is slightly more efficient for Nodes consisting of plain
byte strings, which are very common in MemoDB. It is supported by the `ipfs`
program.

The `dag-cbor-unrestricted` content type is necessary to represent Nodes that
do not meet the [DAG-CBOR] restrictions. It is not supported by other IPLD
programs.

The `dag-cbor` content type is useful to preserve compatibility with the `ipfs`
program for Nodes that meet the [DAG-CBOR] restrictions.

The `cbor` content type is *not* supported because it has limited benefits over
`dag-cbor` and `dag-cbor-unrestricted`.

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

A head name must be a valid Unicode string, and it must not be the empty
string.

## Call

A Call caches the result of applying a function (identified by a textual name)
to one or more Nodes as arguments. When the same function is called on the same
arguments in the future, MemoDB can look up the cached result instead of
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

A function name must be a valid Unicode string, and it must not be the empty
string. Calls must have at least one argument, but this requirement is not yet
enforced.

## References and garbage collection

MemoDB stores can keep track of which Nodes link to which other Nodes. This is
useful for debugging; the `memodb refs-to` and `memodb paths-to` commands can
be used to find which Heads and Calls refer (directly or indirectly) to a given
Node.

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
[CIDs in CBOR]: https://github.com/ipld/cid-cbor/
[CBOR Core Deterministic Encoding]: https://www.rfc-editor.org/rfc/rfc8949.html#name-core-deterministic-encoding
[DAG-CBOR]: https://github.com/ipld/specs/blob/master/block-layer/codecs/dag-cbor.md
[IPLD Data Model]: https://github.com/ipld/specs/blob/master/data-model-layer/data-model.md
[IPLD Schemas]: https://github.com/ipld/specs/tree/master/schemas
[jsoncons]: https://github.com/danielaparker/jsoncons
[libsodium]: https://doc.libsodium.org/
[MemoDB JSON]: ./json.md
[multibase]: https://github.com/multiformats/multibase

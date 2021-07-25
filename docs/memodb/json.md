# JSON format for MemoDB Nodes

<!-- markdownlint-disable MD001 MD024 -->

## Overview

MemoDB Nodes are usually encoded using the CBOR binary encoding, but sometimes
it's necessary or convenient to use a textual format instead. This document
specifies a new format that can unambiguously represent MemoDB Nodes using JSON
values.

#### Rationale

There are two main reasons to have a textual format for Nodes:

1. For diagnostic purposes, to use in debugging dumps, test case inputs, etc.
2. As the main data format for clients that don't have a good CBOR
   implementation available.

For simplicity, it's desirable to use a single format for both purposes.

Several other textual formats were considered and rejected:

- [DAG-JSON] was intended to be used for this purpose, but it sometimes
  prevents the use of `"/"` as a key in maps, which could easily be a problem
  when representing file paths in MemoDB. The exact rules about when `"/"` are
  allowed are somewhat complicated.
- YAML doesn't have a good way to distinguish between the boolean "false" and
  the string "false", the number "1" and the string "1", etc. That's fine when
  you know the schema of the data you're parsing, but MemoDB tools need to work
  with arbitrary Nodes with unknown schemas. One could distinguish between
  `false` for the boolean value and `"false"` for the string, but it's too
  error-prone for humans to remember the exact strings which need to be quoted
  this way.
- TOML works best when the top-level structure of the document is a tree of
  maps, which isn't necessarily true of MemoDB Nodes. It also doesn't have a
  good way to encode CIDs or binary data.
- CBOR's [Diagnostic Notation] or [Extended Diagnostic Notation] would work,
  but are not widely supported. They also format CIDs in an awkward way, using
  `42(h'0001710001f6')` for the CID `uAXEAAfY` for example.


## Special JSON objects

Unfortunately, not every kind of MemoDB Node has a natural mapping to JSON.
This format uses special JSON objects to represent floats, byte strings, and
CIDs. In order to distinguish between these special objects and normal objects
that represent MemoDB maps, normal objects are also wrapped in a special
object.

For example, this MemoDB Node (in a made-up format):

<!-- markdownlint-disable-next-line MD040 -->
```
{
  foo: CID(uAXEAAfY),
  bar: 1.0,
  baz: bytes(0x55, 0xaa),
}
```

would become this JSON:

```json
{
  "map": {
    "foo": {"cid": "uAXEAAfY"},
    "bar": {"float": 1},
    "baz": {"base64": "Vao="}
  }
}
```

#### Rationale

Instead of using special maps to represent CIDs and byte strings, special
arrays could have been used instead. For example, a CID would become `["cid",
"uAXEAAfY"]`, and an empty list would become `["list", []]`. This would be
pretty similar to the current solution overall, but indexing into the
structures would be more confusing (`node.cid` is more clear than `node[1]`).

## Null

```json
null
```

MemoDB null is represented with JSON `null`.

## Booleans

```json
true
false
```

MemoDB booleans are represented with JSON `true` and `false`.

## Integers

```json
1234
-1000000
-9223372036854775808
9223372036854775807
```

MemoDB integers are represented with JSON numbers, without a fraction or an
exponent. The full 64-bit signed integer range must be supported.

#### Rationale

MemoDB supports the full range of 64-bit signed integers, which JSON can
represent perfectly well. Some JSON implementations don't support the full
64-bit integer range because they parse all JSON numbers as floating-point, but
it would be too awkward to work around this problem by using alternative
representations of integers.

## Floats

```json
{"float": 3.142}
{"float": 1}
```

MemoDB floats are represented with a special single-element JSON object, with
the name `"float"` and a value which is a JSON number.

Implementations do **not** need to support floating-point infinities and NaNs,
which are not allowed in MemoDB Nodes.

#### Rationale

Floats could be distinguished from integers by the presence of a fractional
part, so `1` would be an integer and `1.0` would be a float. However, some
useful JSON implementations (such as the `jq` tool, and LLVM's implementation)
do not distinguish between `1` and `1.0`.

## Text strings

```json
"text"
""
"\uD83D\uDE10"
"\u0000"
```

MemoDB text strings are represented with JSON strings. Implementations must
have correct support for arbitrary Unicode characters, including escaped
control characters and non-BMP characters.

## Byte strings

```json
{"base64": "YXNjaWk="}
{"base64": ""}
```

MemoDB byte strings are represented with a special single-element JSON object,
with the name `"base64"` and a value which is a JSON string containing the
base64-encoded bytes. The string must be padded if necessary to reach a
multiple of 4 characters. The string must not contain whitespace or other
extraneous characters.

Note that the name used in this format is `"base64"`, even though Multibase
refers to this encoding as `base64pad`.

#### Rationale

Base64 is probably the most widely supported binary-to-text encoding, and the
base variant (not base64url) is the most widely supported variant.

Padding could be made optional. However, padding is required by some useful
implementations like Python's `base64` module and the Busybox/Coreutils
`base64` command.

The format could allow encodings other than base64, but that would create
difficulties for clients that don't have a Multibase implementation available.

## CIDs

```json
{"cid": "uAXEAAfY"}
```

MemoDB CIDs are represented with a special single-element JSON object, with the
name `"cid"` and a value which is a JSON string containing the string
representation of the CID. Only the `base64url` multibase may be used, so the
string will always start with `u`.

#### Rationale

The `base64url` multibase is the default for MemoDB (see [MemoDB Data
Model](./data-model.md)). It has the advantages of being efficient, and being
suitable for use in URLs without any escaping. It might be nice to use
`base64pad` instead, to match the representation of byte strings, but that
would require CIDs to be reencoded or escaped when used in URLs, which would be
inconvenient for debugging. On the rare occasions when it's necessary to decode
a CID into binary, clients can easily [convert the `base64url` CIDs into
`base64pad`](https://datatracker.ietf.org/doc/html/rfc7515#appendix-C).

The format could allow other multibases to be used, but that would create
difficulties for clients that don't have a Multibase implementation available.

## Lists

```json
["foo", false]
[]
```

MemoDB lists are represented with JSON arrays. Note that elements of the same
array can have different types.

## Maps

```json
{"map": {"key": "value", "key2": "value2"}}
{"map": {}}
```

MemoDB maps are represented with a special single-element JSON object, with the
name `"map"` and a value which is also a JSON object. The inner JSON object
contains names and values corresponding to the keys and values in the MemoDB
map. The order of elements doesn't matter.

#### Rationale

Maps need to be wrapped in a special JSON object in order to distinguish
between a MemoDB map like `{float: 1}` and a MemoDB float like `1.0`. The
former will be represented as `{"map":{"float":1}}`, and the latter will be
represented as `{"float":1}`.

## Determinism

Clients need not produce deterministic JSON for a given Node. They may use
arbitrary whitespace, put object elements in an arbitrary order, and make other
decisions nondeterministically as long as the result is valid JSON.

The MemoDB server will produce deterministic JSON for a given Node, using the
following rules:

- No unnecessary whitespace will be used.
- Object elements will be in the same order that would be used for
  deterministically encoded CBOR. That is, shorter names come before longer
  ones, and names with the same length are ordered lexicographically.
- String characters that MUST be escaped will always be escaped. Aside from
  those, printable ASCII characters will never be escaped. The decision of
  whether to escape other characters will be made in an unspecified but
  deterministic manner.
- The choice between escape codes like `\u000a`, `\u000A`, and `\n` will be
  made in an unspecified but deterministic manner.
- Floats will be formatted in an unspecified but deterministic manner.

#### Rationale

Requiring clients to be deterministic would demand too much extra effort of
them. Clients that prefer to be deterministic can use CBOR instead.

The server needs to produce deterministic JSON in order for the HTTP ETag
header to be valid, allowing cached JSON responses to be validated.

The exact details of the server's JSON encoding are partially unspecified in
order to allow the server to switch to a different JSON implementation in the
future.

[DAG-JSON]: https://github.com/ipld/specs/blob/master/block-layer/codecs/dag-json.md
[Diagnostic Notation]: https://www.rfc-editor.org/rfc/rfc8949.html#name-diagnostic-notation
[Extended Diagnostic Notation]: https://datatracker.ietf.org/doc/html/rfc8610#appendix-G

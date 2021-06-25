# JSON format for MemoDB nodes

## Overview

MemoDB nodes are usually encoded using the CBOR binary encoding, but sometimes
it's necessary or convenient to use a textual format instead. This document
specifies a new format that can unambiguously represent MemoDB nodes using JSON
values.

#### Rationale

Several other formats were considered and rejected:

- [DAG-JSON](https://github.com/ipld/specs/blob/master/block-layer/codecs/dag-json.md)
  was intended to be used for this purpose, but it sometimes prevents the use
  of `"/"` as a key in maps, which could easily be a problem when representing
  file paths in MemoDB. The exact rules about when `"/"` are allowed are
  somewhat complicated.
- YAML doesn't have a human-friendly way to distinguish between the boolean
  "false" and the string "false", the number "1" and the string "1", etc.
  That's fine when you know the schema of the data you're parsing, but MemoDB
  tools need to work with arbitrary nodes with unknown schemas.
- TOML works best when the top-level structure of the document is a tree of
  maps, which isn't necessarily true of MemoDB nodes. It also doesn't have a
  good way to encode CIDs or binary data.
- CBOR's [Diagnostic
  Notation](https://www.rfc-editor.org/rfc/rfc8949.html#name-diagnostic-notation)
  or [Extended Diagnostic
  Notation](https://datatracker.ietf.org/doc/html/rfc8610#appendix-G) would
  work, but are not widely supported. They also format CIDs in an awkward way,
  using `42(h'0001710001f6')` for the CID `bafyqaapw` for example.


## Special JSON objects

Unfortunately, not every kind of MemoDB node has a natural mapping to JSON.
This format uses special JSON objects to represent floats, byte strings, and
CIDs. In order to distinguish between these special objects and normal objects
that represent MemoDB maps, normal objects are also wrapped in a special
object.

For example, this MemoDB node (in a made-up format):

```
{
  foo: CID(bafyqaapw),
  bar: 1.0,
  baz: bytes(0x55, 0xaa),
}
```

would become this JSON:

```json
{
  "map": {
    "foo": {"cid": "bafyqaapw"},
    "bar": {"float": 1},
    "baz": {"base64": "Vao="}
  }
}
```

#### Rationale

Instead of using special maps to represent CIDs and byte strings, special
arrays could have been used instead. For example, a CID would become `["cid",
"bafyqaapw"]`, and an empty list would become `["list", []]`. This would be
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
```

MemoDB integers are represented with JSON numbers, without a fraction or an
exponent.

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
which are not allowed in MemoDB nodes.

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

The format could allow encodings other than base64, but that would create
difficulties for clients that don't have a Multibase implementation available.

Padding could be made optional. However, padding is required by some useful
implementations like Python's `base64` module and the Busybox/Coreutils
`base64` command.

## CIDs

```json
{"cid": "bafyqaapw"}
```

MemoDB CIDs are represented with a special single-element JSON object, with the
name `"cid"` and a value which is a JSON string containing the string
representation of the CID. Only the `base32` multibase may be used, so the
string will always start with `b`.

#### Rationale

The format could allow multibases other than base32, but that would create
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

The format could enforce determinism, specifying element order and other
details so a given MemoDB node would always be encoded as the same JSON string.
However, this would create too much extra work for clients. When determinism is
important, CBOR should be used instead.

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
  allowed are somewhat complicated. It also doesn't support MemoDB's extensions
  to DAG-CBOR, such as 64-bit unsigned integers.
- YAML is actually a set of many possible formats; for example, implementations
  have no consensus whether `on` should be parsed into a boolean or a string.
  That's fine when you know the schema of the data you're parsing, but MemoDB
  tools need to work with arbitrary Nodes with unknown schemas.
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
    "bar": {"float": "1"},
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
18446744073709551615
```

MemoDB integers are represented with JSON numbers, without a fraction or an
exponent. The full 64-bit signed and unsigned integer ranges must be supported.

#### Rationale

MemoDB supports the full range of 64-bit signed and unsigned integers, which
JSON can represent perfectly well. Some JSON implementations don't support the
full range because they parse all JSON numbers as floating-point, but it would
be too awkward to work around this problem by using alternative representations
of integers.

## Floats

```json
{"float": "3.142"}
{"float": "1"}
{"float": "-0"}
{"float": "-1.00000000000000065042509409911827826032367803636410424129692898e-308"}
{"float": "NaN"}
{"float": "Infinity"}
{"float": "-Infinity"}
```

MemoDB floats are represented with a special single-element JSON object, with
the name `"float"` and a value which is a string representation of the float.
The string representation will itself be a valid JSON number, or `NaN`,
`Infinity`, or `-Infinity` (case-sensitive).

#### Rationale

The strings `NaN`, `Infinity`, and `-Infinity` are standard in Javascript, and
supported by at least one JSON implementation (`yq`).

Floats could be distinguished from integers by the presence of a fractional
part, so `1` would be an integer and `1.0` would be a float. However, some
useful JSON implementations (such as the `jq` tool, and LLVM's implementation)
do not distinguish between `1` and `1.0`.

Floats could be represented with special objects containing JSON numbers, like
`{"float": 1}`. However, some useful implementations (such as [PHP] and
Chromium's `JSON.stringify()`) do not distinguish between `0.0` and `-0.0`.
There are also some disagreements between floating point parsers; Ruby rounds
the number
`"-1.00000000000000065042509409911827826032367803636410424129692898e-308"`
incorrectly, for example, although the MemoDB server is unlikely to produce a
value with such an excessive number of digits.

All in all, the best way to ensure the JSON is a faithful representation of the
Node is to avoid the use of the client's floating-point parser and formatter.
If you don't like it, switch to CBOR, which avoids all of these issues.

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
future. It might be nice to use the rules in [RFC 8785], but some of the
requirements there are a poor match for MemoDB (for instance, the required use
of UTF-16 for sorting).

[DAG-JSON]: https://github.com/ipld/specs/blob/master/block-layer/codecs/dag-json.md
[Diagnostic Notation]: https://www.rfc-editor.org/rfc/rfc8949.html#name-diagnostic-notation
[Extended Diagnostic Notation]: https://datatracker.ietf.org/doc/html/rfc8610#appendix-G
[PHP]: https://github.com/php/php-src/pull/7234
[RFC 8785]: https://www.rfc-editor.org/info/rfc8785

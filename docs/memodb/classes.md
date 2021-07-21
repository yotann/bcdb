# MemoDB C++ classes

## Utility classes

- `memodb::Multibase`: Converts between raw bytes and [Multibase]-encoded text,
  in formats such as base32 and base64url. Can also encode and decode these
  formats directly, without the multibase prefix.
- `memodb::URI`: Decodes and encodes URIs.

## Content-addressable store

- `memodb::CID`: Represents a single [CID] used to uniquely identify a Node.
- `memodb::Node`: Represents a single node in the [MemoDB data model]. Can be
  converted to and from CBOR bytes and JSON text. The API is based on
  [jsoncons::basic_json]. Users can specialize `memodb::NodeTypeTraits` in
  order to easy convert their data types to and from Nodes.
- `memodb::Store`: Accesses a database following the [MemoDB data model].
- `memodb::NodeRef`: Represents a stored Node for which either the CID or the
  Node value is known, or both. The value is automatically loaded from or
  stored in the Store as necessary.

## Func evaluation

- `memodb::Evaluator`: evaluates funcs using a thread pool. Funcs must be
  registered with the Evaluator before use. Evaluation results are
  automatically cached in the Store.

## REST server

- `memodb::Server`: Implements a REST server used to access a Store. In the
  future, this will also support distributed computing for func evaluation.
- `memodb::Request`: Protocol-independent abstract class representing a single
  REST request for the Server to respond to. In addition to HTTP/1, this class
  could be overridden to support CoAP, HTTP/2, or a custom protocol.
- `memodb::HTTPRequest`: HTTP-specific subclass of Request. Each supported HTTP
  library will have its own subclass of HTTPRequest.

[CID]: https://github.com/multiformats/cid
[jsoncons::basic_json]: https://github.com/danielaparker/jsoncons/blob/master/include/jsoncons/basic_json.hpp
[MemoDB data model]: ./data-model.md
[MemoDB JSON]: ./json.md
[Multibase]: https://github.com/multiformats/multibase

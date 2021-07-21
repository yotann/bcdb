# MemoDB

A content-addressable store and a memoizing distributed processing framework.

Features:

- A content-addressable, DAG-based data store for arbitrary structured data.
  Kinda like IPFS or Git.
- Automatic deduplication when duplicate data is added to the store.
- A framework for using distributed processing to evaluate functions on data in
  the store.
- Support for automatically caching (memoizing) the results of function
  evaluation.
- A REST API for accessing the store and writing distributed workers.

## Design

See the [MemoDB data model].

## Usage

- From the command line: use the `memodb` program. (No documentation yet.)
- From C++: see the [C++ class list] and look through the header files.
- From other languages: see the [REST API documentation].

## Rationales

See the [MemoDB data model] and [comparisons of protocols and libraries].

[C++ class list]: ./classes.md
[comparisons of protocols and libraries]: ./comparisons.md
[MemoDB data model]: ./data-model.md
[REST API documentation]: ./rest-api.md

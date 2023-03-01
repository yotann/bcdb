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

## Tutorial

See the [Tutorial].

## Debugging

See [debugging].

## Design

See the [MemoDB data model].

## Usage

- From the command line: use the `memodb` program. See the [Tutorial].
- From C++: see the [C++ class list] and look through the header files. You can
  also generate documentation by running `doxygen Doxyfile` in the top-level
  BCDB directory; documentation will go in `build/doxygen/html`.
- From other languages: see the [REST API documentation].

## Rationales

See the [MemoDB data model] and [comparisons of protocols and libraries].

[C++ class list]: ./docs/classes.md
[comparisons of protocols and libraries]: ./docs/comparisons.md
[debugging]: ./docs/debugging.md
[MemoDB data model]: ./docs/data-model.md
[REST API documentation]: ./docs/rest-api.md
[Tutorial]: ./docs/tutorial.md
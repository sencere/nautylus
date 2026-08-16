# Status

Current deployability: **usable alpha**.

The project builds, tests, and produces a working `nautylus` CLI plus an embeddable C99 library for the implemented features. It is not feature-complete against the full specification in `agent.md`.

## Capability Summary

| Area | Implemented now | Remaining |
| --- | --- | --- |
| C99 foundation | Strict C99 build, typed values, dynamic storage, deterministic symbols, CRUD, validation, tests, CLI, shared library build, lightweight Python/PHP FFI bindings | Broader allocator hooks, more malformed-record coverage, broader language binding surface |
| Graph representation | Directed relationships, labels, typed properties, enumeration, bounded breadth-first traversal, validation, incident-edge cleanup, rebuilt adjacency cache | Incremental adjacency maintenance, depth-first traversal ordering |
| Import/export | Triple TSV/CSV, property-graph TSV, typed values, duplicate suppression, diagnostics, import rollback, deterministic export ordering, CLI workflows, `.nautylusbak` export guards | Stronger two-file crash recovery, more CLI flags |
| Persistence | Portable single-file snapshots, little-endian encoding, versioned header, checksum, persisted node-property constraints, temporary-file write, pre-save validation, strict load checks | Per-section checksums, generation metadata, migrations, stronger durability semantics |
| Query | Property retrieval, label checks, exact node scans, snapshot node indexes, persistent exact-match index metadata, persisted required/unique property constraints, property-aware node creation API, property-mutation constraint enforcement, bounded traversal, multi-node MiniCypher, `WHERE`, `WITH`, `UNWIND`, `OPTIONAL MATCH`, parameters, aggregates, `ORDER BY`, `SKIP`/`LIMIT`, `UNION`/`UNION ALL`/`UNION DISTINCT`, rollback-protected `CREATE`/`MERGE`/`SET`/`REMOVE`/`DELETE`/`DETACH DELETE`, nested map expressions in projections and writes, list indexing/slicing/concatenation/comprehensions, searched `CASE`, fixed and bounded variable-length path bindings with `nodes()`/`relationships()`, generic `MERGE` `ON CREATE SET`/`ON MATCH SET`, graph-registered procedures with typed node/relationship arguments and result aliases, seeded `randomWalk` procedure, `EXPLAIN` text | Full Cypher compatibility, direct path rendering, subqueries |
| Transactions/indexes | Public in-memory transaction API, commit, rollback, persistent index metadata, snapshot node index rebuilding | Multi-process conflicts, durable transaction journal, materialized persistent indexes |
| Release quality | Strict C99 tests, CLI regression coverage, documented tested limits, small local performance baseline, ASan/UBSan run with LeakSanitizer disabled in this environment | CI, fuzzing, profiling |
| Web/server | Local POSIX HTTP workbench for stats, query/explain, triple import, sample data, constraints, index metadata, interactive graph rendering, node/relationship inspection, typed node properties, and label color editing | Broader API, non-POSIX support |
| Analytics | Degree centrality, PageRank, eigenvector, closeness, and harmonic centrality, FastRP-style seeded embeddings, lightweight Node2Vec- and GraphSAGE-style embeddings, configurable GraphSAGE model inference/training with sampling, normalization, mini-batches, validation splits, compact sampled subgraph training, cached sampled-neighborhood reuse, reusable gradient buffers, analytic MSE, binary cross-entropy, and softmax cross-entropy backpropagation, optimized split reporting, epoch diagnostics, convergence status, validation-selection reporting, classification metrics, prediction helpers, model save/load, exact vector-index persistence, approximate random-projection vector search with tunable candidates, flat indexed ANN graph search, HNSW-style multi-layer ANN indexing/search with tunable `M`/`efConstruction`/`efSearch` and persistence, and cosine vector search, weak/strong components, triangle count, local clustering coefficient, articulation points, bridges, common-neighbor, Adamic-Adar, and resource-allocation link prediction, topological sort, minimum spanning tree, maximum flow, seeded random walks, weighted Dijkstra, unweighted BFS, callback-based DFS path enumeration, heuristic-driven A*, deterministic label propagation, Louvain-style local moving, Jaccard KNN similarity, and label-filtered KNN | Multilevel Louvain/Leiden aggregation, richer filtered similarity, scalable implementations |

## Current CLI

```text
nautylus create FILE
nautylus open FILE
nautylus help
nautylus version
nautylus validate FILE
nautylus stats FILE
nautylus analyze FILE
nautylus analyse FILE
nautylus store DB TRIPLES
nautylus import DB TRIPLES
nautylus store-csv DB TRIPLES_CSV
nautylus import-csv DB TRIPLES_CSV
nautylus export DB TRIPLES
nautylus export DB -
nautylus store-ng DB NODES RELATIONSHIPS
nautylus import-ng DB NODES RELATIONSHIPS
nautylus export-ng DB NODES RELATIONSHIPS
nautylus constraint-require DB LABEL KEY
nautylus constraint-unique DB LABEL KEY
nautylus constraint-drop-require DB LABEL KEY
nautylus constraint-drop-unique DB LABEL KEY
nautylus constraints DB
nautylus index-create DB LABEL KEY
nautylus index-drop DB LABEL KEY
nautylus indexes DB
nautylus bench FILE NODE_COUNT
nautylus serve DB PORT
nautylus search DB QUERY
nautylus query DB QUERY [--format auto|verbose|plain|json]
nautylus explain QUERY
```

## Latest Evidence

`make test` passes under:

```sh
cc -std=c99 -Wall -Wextra -Wpedantic -O2
```

An ASan/UBSan build also passes when run as:

```sh
ASAN_OPTIONS=detect_leaks=0 ./build/test_nautylus
```

LeakSanitizer itself is disabled for that run because this execution environment reports ptrace incompatibility.

Regression coverage includes:

* native save/reopen;
* strict snapshot payload and trailing-byte checks;
* symbol and record identity;
* typed property retrieval;
* typed exact-match scans;
* snapshot node index lookups;
* persistent exact-match index metadata;
* required and unique node-property validation;
* persisted required and unique node-property constraints;
* property-aware node creation with required/unique constraint preflight;
* property-mutation constraint enforcement;
* MiniCypher node, relationship, bounded path, projection, expression, procedure, and union parsing/execution;
* MiniCypher `CREATE`, `MERGE`, `SET`, `REMOVE`, `DELETE`, and `DETACH DELETE` writes with transaction rollback;
* map-based `SET +=` merge and `SET =` replacement with null-removal semantics;
* `WITH`, `UNWIND`, `OPTIONAL MATCH`, parameters, aggregates, ordering, `SKIP`, and `LIMIT`;
* comma-separated `CREATE` and `MERGE` pattern lists;
* seeded C API and Cypher random walks;
* MiniCypher explain output;
* label checks;
* relationship enumeration;
* bounded traversal over cycles and parallel edges;
* deletion with incident-edge cleanup;
* property deletion;
* ID non-reuse;
* TSV and CSV triple import with duplicate suppression and rollback;
* triple export to stdout;
* property-graph import diagnostics;
* property-graph import rollback under allocation-failure injection;
* typed value encode/decode cases;
* malformed typed values;
* deterministic repeated exports;
* typed property-graph round trips;
* CLI property-graph import/export/validate/stats;
* CLI benchmark smoke path;
* local web workbench build coverage;
* graph rendering and inspector property propagation;
* sorted label/property views;
* backup-file export guards;
* ordering-buffer allocation failure;
* transaction commit and rollback.

## Roadmap

Near-term:

1. Add platform-specific export rename failure tests.
2. Expand malformed-record and constraint edge-case coverage.
3. Add scoped subqueries and procedure signatures.

Larger product directions:

* stronger snapshot migration policy;
* CI across GCC/Clang and supported operating systems;
* fuzzing;
* richer HTTP/API/web interface outside the core library.

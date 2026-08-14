# Status

Current deployability: **usable alpha**.

The project builds, tests, and produces a working `nautylus` CLI plus an embeddable C99 library for the implemented features. It is not feature-complete against the full specification in `agent.md`.

## Capability Summary

| Area | Implemented now | Remaining |
| --- | --- | --- |
| C99 foundation | Strict C99 build, typed values, dynamic storage, deterministic symbols, CRUD, validation, tests, CLI | Broader allocator hooks, more malformed-record coverage |
| Graph representation | Directed relationships, labels, typed properties, enumeration, bounded breadth-first traversal, validation, incident-edge cleanup, rebuilt adjacency cache | Incremental adjacency maintenance, depth-first traversal ordering |
| Import/export | Triple TSV/CSV, property-graph TSV, typed values, duplicate suppression, diagnostics, import rollback, deterministic export ordering, CLI workflows, `.nautylusbak` export guards | Stronger two-file crash recovery, more CLI flags |
| Persistence | Portable single-file snapshots, little-endian encoding, versioned header, checksum, persisted node-property constraints, temporary-file write, pre-save validation, strict load checks | Per-section checksums, generation metadata, migrations, stronger durability semantics |
| Query | Property retrieval, label checks, exact node scans, snapshot node indexes, persistent exact-match index metadata, persisted required/unique property constraints, property-aware node creation API, property-mutation constraint enforcement, bounded traversal, multi-node MiniCypher, `WHERE`, `WITH`, `UNWIND`, `OPTIONAL MATCH`, parameters, aggregates, `ORDER BY`, `SKIP`/`LIMIT`, `UNION`/`UNION ALL`/`UNION DISTINCT`, rollback-protected `CREATE`/`MERGE`/`SET`/`REMOVE`/`DELETE`/`DETACH DELETE`, nested map expressions in projections and writes, graph-registered procedures with typed node/relationship arguments and result aliases, seeded `randomWalk` procedure, `EXPLAIN` text | Full Cypher compatibility, path values, subqueries |
| Transactions/indexes | Public in-memory transaction API, commit, rollback, persistent index metadata, snapshot node index rebuilding | Multi-process conflicts, durable transaction journal, materialized persistent indexes |
| Release quality | Strict C99 tests, CLI regression coverage, documented tested limits, small local performance baseline, ASan/UBSan run with LeakSanitizer disabled in this environment | CI, fuzzing, profiling |
| Web/server | Local POSIX HTTP workbench for stats, query/explain, triple import, sample data, constraints, index metadata, interactive graph rendering, node/relationship inspection, typed node properties, and label color editing | Broader API, non-POSIX support |
| Analytics | Degree centrality, PageRank, weak/strong components, triangle count, local clustering coefficient, common-neighbor link prediction basics, topological sort, seeded random walks | Weighted paths, community detection, KNN/similarity, embeddings, max flow, scalable implementations |

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
nautylus query DB QUERY
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
* MiniCypher node and bounded relationship query parsing and execution;
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
3. Add richer list expressions and procedure metadata.

Larger product directions:

* stronger snapshot migration policy;
* CI across GCC/Clang and supported operating systems;
* fuzzing;
* richer HTTP/API/web interface outside the core library.

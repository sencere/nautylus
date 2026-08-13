# Implementation Status

Current deployability: **usable alpha**.

Nautylus currently provides:

* Portable C99 property-graph storage with typed properties, labels, directed relationships, snapshots, validation, import/export, indexes, constraints, and rollback-capable transactions.
* MiniCypher reads and writes including `MATCH`, `CREATE`, `MERGE`, scalar and map-based `SET`, `REMOVE`, `DELETE`, `DETACH DELETE`, `WITH`, `UNWIND`, `OPTIONAL MATCH`, parameters, aggregates, `ORDER BY`, `SKIP`, `LIMIT`, `UNION`, and `UNION ALL`.
* Atomic write execution across the supported write clauses.
* Graph analytics including PageRank, centrality, components, triangle counting, clustering, link-prediction basics, topological sorting, and seeded random walks.
* A local web workbench with graph rendering, query execution, node/relationship inspection, typed property display, and editable label colors.

The project is not compatible with full Neo4j/Cypher. Major remaining areas include path values, subqueries, weighted path algorithms, community detection, similarity, embeddings, flow algorithms, fuzzing, CI, and large-scale performance work.

`make test` passes with the strict C99 build configuration. See [STATUS.md](STATUS.md) for detailed capability evidence and roadmap status.

# Implementation Status

Current deployability: **usable alpha**.

Nautylus currently provides:

* Portable C99 property-graph storage with typed properties, labels, directed relationships, snapshots, validation, import/export, indexes, constraints, and rollback-capable transactions.
* MiniCypher reads and writes including `MATCH`, `CREATE`, `MERGE`, scalar and map-based `SET`, `REMOVE`, `DELETE`, `DETACH DELETE`, `WITH`, `UNWIND`, `OPTIONAL MATCH`, parameters, aggregates, `ORDER BY`, `SKIP`, `LIMIT`, `UNION`, `UNION ALL`, `UNION DISTINCT`, list/map expressions, path bindings, procedures, and `MERGE` `ON CREATE` / `ON MATCH` property updates.
* Atomic write execution across the supported write clauses.
* Graph analytics including PageRank, centrality, components, triangle counting, clustering, link-prediction basics, topological sorting, weighted paths, flow, MST, seeded random walks, FastRP, Node2Vec-style embeddings, GraphSAGE inference/training, and exact/approximate/flat-ANN/HNSW vector search.
* A local web workbench with graph rendering, query execution, node/relationship inspection, typed property display, and editable label colors.

The project is not compatible with full Neo4j/Cypher. Major remaining areas include scoped subqueries, direct path rendering polish, multilevel Louvain/Leiden aggregation, richer filtered similarity, fuzzing, CI, and large-scale performance work.

`make test` passes with the strict C99 build configuration. See [STATUS.md](STATUS.md) for detailed capability evidence and roadmap status.

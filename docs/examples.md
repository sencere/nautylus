# Examples

The `examples/` directory is the fastest way to see Nautylus features in context.

Build all C examples:

```sh
make examples
```

Run examples from the repository root:

```sh
./build/basic
./build/cypher
./build/analytics
./build/graphsage_vector
```

Run language binding examples:

```sh
PYTHONPATH=bindings/python python3 bindings/python/example.py
php bindings/php/example.php
```

## Direct Graph API

Use `examples/basic.c` for the smallest checked C program. It creates labels,
relationship types, nodes, a relationship, a string property, and saves a native
snapshot.

Relevant docs:

* [C API Notes](api.md)
* [Snapshot Format](snapshot-format.md)

## MiniCypher

Use `examples/cypher.c` to call the write-capable query API with parameters and
transactional writes. It demonstrates `CREATE`, named parameters, `UNWIND`,
`OPTIONAL MATCH`, `WITH`, aggregation, `ORDER BY`, and map-based `SET`.

Use `examples/cypher_gallery.cypher` as pasteable query reference for the CLI or
web interface. Run one statement at a time in the web workbench.

Useful CLI forms:

```sh
./build/nautylus query graph.ng 'MATCH (n) RETURN n LIMIT 20' --format verbose
./build/nautylus query graph.ng 'MATCH (n) RETURN n.name' --format plain
./build/nautylus query graph.ng 'MATCH (n) RETURN n' --format json
```

## Analytics

Use `examples/analytics.c` for graph algorithms:

* centrality: degree, PageRank, eigenvector, closeness, harmonic;
* components and communities: weak/strong components, label propagation,
  Louvain-style local moving;
* structure: triangle count, local clustering coefficient, articulation points,
  bridges, topological sort;
* paths and flows: BFS, Dijkstra, A*, path enumeration, random walk, minimum
  spanning tree, maximum flow;
* similarity and link prediction: KNN, filtered KNN, common neighbors,
  preferential attachment, total neighbors, Adamic-Adar, resource allocation.

## GraphSAGE And Vector Search

Use `examples/graphsage_vector.c` for trainable embeddings and vector search.
It demonstrates:

* GraphSAGE model creation;
* supervised training with diagnostics;
* inference;
* class/probability prediction helpers;
* direct exact cosine search;
* reusable vector indexes;
* random-projection approximate search;
* flat ANN search;
* HNSW-style multi-layer search;
* model and vector-index persistence.

Relevant docs:

* [GraphSAGE](graphsage.md)
* [Tested Limits](limits.md)

## CLI Workflow

Use `examples/cli_workflow.sh` for an end-to-end command sequence. It creates a
database, inserts data, runs tabular and JSON queries, prints stats, and
validates the snapshot.

## Language Bindings

Use `bindings/python/example.py` and `bindings/php/example.php` for lightweight
FFI usage from Python and PHP. See [Language Bindings](bindings.md).

# Nautylus Examples

Build all C examples:

```sh
make examples
```

Run them from the repository root:

```sh
./build/basic
./build/cypher
./build/analytics
./build/graphsage_vector
```

The examples intentionally create small local files such as `example.ng`,
`cypher-example.ng`, `analytics-example.ng`, `graphsage-example.model`, and
`graphsage-vectors.ngv`.

## Files

| File | Shows |
| --- | --- |
| `basic.c` | Direct C API graph creation, symbols, labels, relationships, properties, save |
| `cypher.c` | `ng_query_execute_params()`, parameters, transactional writes, `UNWIND`, `WITH`, aggregation, `OPTIONAL MATCH`, `ORDER BY`, JSON-ish CLI-friendly output through the C API |
| `analytics.c` | Centrality, components, triangles, clustering, paths, random walks, link prediction, KNN, MST, max flow, label propagation, Louvain-style communities |
| `graphsage_vector.c` | GraphSAGE inference/training diagnostics, prediction helpers, exact vector search, random-projection approximate search, flat ANN search, HNSW-style search, vector-index persistence |
| `cypher_gallery.cypher` | Pasteable MiniCypher examples for the CLI or web interface |
| `cli_workflow.sh` | End-to-end CLI command examples |

## CLI Query Formats

```sh
./build/nautylus query cypher-example.ng 'MATCH (n) RETURN n' --format verbose
./build/nautylus query cypher-example.ng 'MATCH (n) RETURN n.name' --format plain
./build/nautylus query cypher-example.ng 'MATCH (n) RETURN n' --format json
```

The web interface accepts one query at a time. Use the CLI or a small C wrapper
when you want to run a sequence of statements programmatically.

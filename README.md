# nautylus

<p align="center">
  <img src="resources/logo/logo.png" alt="nautylus logo" />
</p>

**nautylus** is a compact embedded property-graph database written in portable C99. It stores labeled nodes, typed directed relationships, and typed properties in a portable single-file snapshot.

`nautylus` is intended for small embedded applications, command-line tools, tests, and data-processing workflows that need a persistent property graph without running a database server or adding an external dependency.

**Status:** Usable alpha. The implemented subset is tested and usable, but multi-process writer coordination, large-graph performance work, and full Cypher compatibility are not claimed yet. MiniCypher now includes read clauses, write clauses, parameters, `WITH`, `OPTIONAL MATCH`, aggregates, ordering, rollback-protected writes, exact-match indexes, persisted required/unique property constraints, graph analytics APIs, and a local web workbench. See [STATUS.md](STATUS.md) for detailed implementation evidence.

`nautylus` is inspired by Neo4j's property-graph model, but it does not implement Neo4j storage formats, Bolt, or full Cypher.

## Why nautylus

Choose `nautylus` when you want:

* a small C99 graph library that is easy to inspect;
* a CLI that can import, validate, and export graph data;
* deterministic, text-based interchange formats for tests and data pipelines;
* a single-file portable snapshot with explicit validation;
* no database server, background process, or runtime dependency.

Current scale model:

* The whole graph is loaded into memory by `ng_open()`.
* Mutations are in-memory until `ng_save()` succeeds.
* `ng_close()` does not save automatically.
* The regression suite currently covers small graphs, deterministic ordering, rollback behavior, typed values, and CLI workflows. No large-graph performance envelope is claimed yet.
* A small local performance smoke baseline is available with `make perf`; see [docs/limits.md](docs/limits.md).
* A `ng_graph` is not documented as thread-safe. Use one graph from one thread at a time unless you add external synchronization.
* There is no multi-process writer coordination.

## Build

```sh
make
```

This builds `build/nautylus.o` for embedding and `build/nautylus` for command-line use.
It also builds `build/libnautylus.so` for the Python and PHP FFI bindings.

Run tests:

```sh
make test
```

Build examples:

```sh
make examples
```

The example folder now contains runnable coverage for the main surfaces:
`basic.c` for direct graph mutation, `cypher.c` for parameterized MiniCypher,
`analytics.c` for graph algorithms, `graphsage_vector.c` for GraphSAGE and
vector search, `cypher_gallery.cypher` for pasteable web/CLI queries, and
`cli_workflow.sh` for an end-to-end command-line workflow. See
[examples/README.md](examples/README.md).

Run binding examples:

```sh
PYTHONPATH=bindings/python python3 bindings/python/example.py
php bindings/php/example.php
```

The Python binding uses standard-library `ctypes`. The PHP binding uses PHP FFI,
so PHP must have FFI enabled. See [docs/bindings.md](docs/bindings.md).

Run the local performance smoke baseline:

```sh
make perf
```

The default build uses:

```sh
cc -std=c99 -Wall -Wextra -Wpedantic -O2
```

## Five-Minute Quick Start

This workflow is covered by the regression suite.

```sh
make

./build/nautylus create graph.ng

printf 'alice\tKNOWS\tbob\n' > triples.tsv
./build/nautylus store graph.ng triples.tsv

./build/nautylus search graph.ng 'MATCH (n) RETURN n LIMIT 10'
./build/nautylus analyze graph.ng
./build/nautylus export graph.ng -
```

Expected `analyze` output:

```text
status: ok
nodes: 2
relationships: 1
symbols: 2
```

Expected export output:

```text
alice	KNOWS	bob
```

Validate a database:

```sh
./build/nautylus validate graph.ng
```

Expected output:

```text
ok
```

## CLI Reference

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

Notes:

* `nautylus create` creates a native database snapshot.
* `nautylus open` opens and validates a database.
* `nautylus store` imports triple TSV files and saves the database.
* `nautylus import` imports triple TSV files.
* `nautylus store-csv` imports triple CSV files and saves the database.
* `nautylus import-csv` imports triple CSV files.
* `nautylus store-ng` imports explicit property-graph node and relationship TSV files and saves the database.
* `nautylus help` and `nautylus --help` print command usage.
* `nautylus version` and `nautylus --version` print the current alpha version string.
* `nautylus export DB -` writes triples to standard output.
* `nautylus import-ng` imports explicit property-graph node and relationship TSV files.
* `nautylus export-ng` writes explicit property-graph node and relationship TSV files.
* `nautylus constraint-require` stores a required node-property constraint for a label and key.
* `nautylus constraint-unique` stores a unique node-property constraint for a label and key.
* `nautylus constraints` lists stored node-property constraints.
* `nautylus index-create` stores exact-match node-index metadata for a label and key.
* `nautylus index-drop` removes exact-match node-index metadata for a label and key.
* `nautylus indexes` lists stored node-index metadata.
* `nautylus bench` creates a deterministic benchmark graph, saves/reopens it, validates it, builds an exact-match node index, and prints local timing.
* `nautylus serve` starts a local browser workbench for querying, importing triples, creating sample data, and managing simple schema metadata.
* `nautylus search` runs the current MiniCypher subset.
* `nautylus query` runs the current MiniCypher subset. `--format auto` uses a table for terminal output and plain tab-separated values when redirected; `--format verbose` always uses the table; `--format plain` always emits scripting-friendly values only; and `--format json` emits a machine-readable result envelope.
  JSON responses have the shape `{"columns":[...],"rows":[[...]],"row_count":N}`. Result cells are currently JSON strings preserving the CLI rendering, which is suitable for code/template text and Vim integrations.
* `nautylus analyze` and `nautylus analyse` validate the database and print graph counts.
* `nautylus explain` prints the simple selected query plan.
* Exit status is `0` on success and non-zero on failure.
* Malformed property-graph imports report line and column diagnostics where available.

Example malformed property-graph input:

```text
node		Person	name=s:416c696365
```

Typical error shape:

```text
parse error at line 1 column 1
parse error
```

## C API Example

Link against `build/nautylus.o` and include `src/nautylus.h`:

```sh
cc -std=c99 -Wall -Wextra -Wpedantic -O2 -Isrc my_app.c build/nautylus.o -o my_app
```

Minimal checked example:

```c
#include "nautylus.h"
#include <stdio.h>
#include <string.h>

#define NG_CHECK(expr) do { \
    status = (expr); \
    if (status != NG_OK) { \
        fprintf(stderr, "%s\n", ng_status_name(status)); \
        goto fail; \
    } \
} while (0)

int main(void) {
    ng_graph *g = 0;
    ng_status status = NG_OK;
    ng_symbol_id person = 0, knows = 0, name = 0;
    ng_node_id alice = 0, bob = 0;
    ng_relationship_id rel = 0;
    ng_value value;

    NG_CHECK(ng_create(&g, "example.ng"));
    NG_CHECK(ng_symbol(g, "Person", &person));
    NG_CHECK(ng_symbol(g, "KNOWS", &knows));
    NG_CHECK(ng_symbol(g, "name", &name));

    NG_CHECK(ng_node_create(g, &person, 1, &alice));
    NG_CHECK(ng_node_create(g, &person, 1, &bob));
    NG_CHECK(ng_relationship_create(g, alice, knows, bob, &rel));

    memset(&value, 0, sizeof(value));
    value.type = NG_VALUE_STRING;
    value.length = 5;
    value.as.string = "Alice";
    NG_CHECK(ng_node_set(g, alice, name, &value));

    NG_CHECK(ng_save(g));
    ng_close(g);
    return 0;

fail:
    ng_close(g);
    return 1;
}
```

More API details are in [docs/api.md](docs/api.md).
The GraphSAGE-style embedding API is documented in [docs/graphsage.md](docs/graphsage.md).

## Graph Analytics API

The C API includes an initial dependency-free analytics layer for small in-memory graphs:

```c
ng_node_score scores[128];
size_t count = 0;

ng_pagerank(g, knows, 0.85, 25, scores, 128, &count);
ng_degree_centrality(g, NG_DIRECTION_EITHER, knows, scores, 128, &count);

ng_random_walk_options walk = { NG_DIRECTION_OUTGOING, knows, 10, 42 };
ng_node_id path[11];
ng_random_walk(g, start, &walk, path, 11, &count);
```

Implemented algorithms:

* degree centrality: incoming, outgoing, or either direction;
* PageRank: directed, unweighted, with optional relationship-type filter;
* weakly connected components;
* strongly connected components;
* triangle count;
* local clustering coefficient;
* link prediction basics: common neighbors, preferential attachment, total neighbors;
* topological sort with `NG_EXISTS` returned for cyclic graphs.
* seeded random walks with outgoing, incoming, or undirected relationship traversal;

All analytics APIs operate on the current in-memory graph and write results into caller-owned arrays. Pass `type = 0` to include all relationship types, or a relationship symbol ID to filter by type. If the output capacity is too small, the call returns `NG_LIMIT` and reports the required count when an `out_count` pointer is supplied.

Weighted Dijkstra, unweighted BFS shortest paths, callback-based simple-path enumeration, heuristic-driven A*, deterministic label propagation, a Louvain-style local-moving pass, eigenvector, closeness, and harmonic centrality, FastRP-style seeded embeddings, lightweight Node2Vec- and GraphSAGE-style embeddings, configurable GraphSAGE model inference/training with analytic MSE, binary cross-entropy, and softmax cross-entropy backpropagation, minimum spanning trees, maximum flow, Jaccard KNN similarity, label-filtered KNN, Adamic-Adar, Resource Allocation link prediction, and exact/approximate/HNSW vector search are available through the C API. Full multilevel Louvain/Leiden aggregation, richer filtered similarity, and large-scale optimized centrality are not implemented yet.

For GraphSAGE-style embeddings, provide one row of numeric features per node and receive a row-major embedding matrix. Reusable models support sampled multi-layer inference, analytic training with compact sampled subgraphs and reusable gradient buffers, optimized split reporting, epoch diagnostics, convergence status, validation split details, classification metrics, prediction helpers, normalization, save/load, exact/approximate/flat-ANN/HNSW vector-index persistence, and cosine search. See [docs/graphsage.md](docs/graphsage.md) for the complete call contract and working examples.

## MiniCypher Subset

The current query parser intentionally supports a practical subset of Cypher, not full Neo4j Cypher. Supported read patterns include node matches, multi-hop relationship matches, multi-node paths, `WHERE`, `WITH`, `UNWIND`, `OPTIONAL MATCH`, aggregation, `ORDER BY`, `SKIP`, and `LIMIT`:

```text
MATCH (n) RETURN n
MATCH (n) RETURN n.id
MATCH (n:Label) RETURN n.key
MATCH (n:Label) RETURN n
MATCH (n:Label) WHERE n.key = "value" RETURN n
MATCH (n:Label) WHERE id(n) = 1 RETURN n
MATCH (n:Label) WHERE n.id = 1 RETURN n.key
MATCH (n) RETURN n LIMIT 10
MATCH (n)-[:TYPE]->(m) RETURN m
MATCH (n)-[:TYPE*1..3]->(m) RETURN m
MATCH (n)-[:TYPE]->(m) RETURN n.key, m.key
MATCH (n:Label)-[:TYPE]->(m:Label) WHERE m.key = "value" RETURN n LIMIT 10
MATCH (a:Person) WITH a MATCH (a)-[:KNOWS]->(b) RETURN a.name, b.name
MATCH (a:Person) OPTIONAL MATCH (a)-[:KNOWS]->(b) RETURN a.name, b.name
UNWIND [1, 2, 3] AS value RETURN value
MATCH (a:Person) RETURN a.city, count(a) AS people ORDER BY people DESC
MATCH (a:Person) CALL randomWalk(a, 5, 42) YIELD node RETURN node
MATCH p=(a:Person)-[r:KNOWS]->(b:Person) RETURN nodes(p), relationships(p)
```

`CALL randomWalk(start, steps[, seed]) YIELD node` expands each incoming row into one row per visited node, including the start node. The Cypher adapter currently uses outgoing relationships and all relationship types; the typed C API provides direction and relationship-type filters.

Supported scalar values are strings, integers, doubles, booleans, `null`, and lists produced by list literals, list-valued parameters, graph properties, or `collect(...)`. List expressions support indexing, negative indexes, slicing with inclusive start/exclusive end bounds, list concatenation with `+`, list comprehensions such as `[x IN xs WHERE x > 1 | x * 2]`, searched and simple `CASE`, and `size`, `head`, `last`, `tail`, `reverse`, `toString`, `coalesce`, `toLower`, `toUpper`, `trim`, and `abs`. `UNWIND <list-expression> AS variable` expands one input row per list item; empty and null lists produce no rows. Predicate support includes `=`, `<>`, `<`, `<=`, `>`, `>=`, `IN`, `IS NULL`, `IS NOT NULL`, `AND`, `OR`, `NOT`, and parentheses. Relationship reads support `->`, `<-`, and undirected `-[]-` patterns. Exact or bounded hop counts from 1 to 64 are supported in read relationship patterns, such as `*2` or `*1..3`.

Projection support includes variables, IDs, property access, literals, parameters, simple arithmetic, aliases with `AS`, `DISTINCT`, and tab-separated multi-column output. `ORDER BY` works after `WITH` and final `RETURN`, supports multiple keys and `ASC`/`DESC`, and executes after projection/aggregation and `DISTINCT`, before `SKIP`/`LIMIT`. Null ordering is deterministic: nulls sort last for ascending order and first for descending order.

Aggregate support:

```text
MATCH (a:Person) RETURN count(*)
MATCH (a:Person) RETURN count(a), sum(a.age), collect(a.name)
MATCH (a:Person) RETURN a.city, count(a)
MATCH (a:Person) WITH a.city AS city, count(a) AS people WHERE people > 1 RETURN city, people
```

Supported aggregates are `count(*)`, `count(expr)`, `sum(expr)`, and `collect(expr)`. `count(expr)`, `sum(expr)`, and `collect(expr)` ignore null values. `sum(...)` returns `null` when there are no non-null numeric inputs. `DISTINCT` is supported inside `count(...)` and `collect(...)`.

Write queries are rollback-protected by the transaction layer:

```text
CREATE (a:Person {name: "Joe"})-[:KNOWS]->(b:Person {name: "Bob"})
CREATE (a:Person {name: "A"}), (b:Person {name: "B"})
MATCH (a:Person) WHERE a.name = "A" SET a.city = "Berlin", a.score = 10
MATCH (a:Person)-[r:KNOWS]->(b:Person) DELETE r, b
MATCH (a:Person) REMOVE a.name, a:Person
MATCH (a:Person) DETACH DELETE a
MATCH (a:Person) SET a += {city: "Berlin", score: 10}
MATCH (a:Person) SET a = {name: "Replacement"}
MERGE (a:Person {name: "A"}), (b:Person {name: "B"}), (a)-[:KNOWS]->(b)
MERGE (n:Person {name: "A"}) ON CREATE SET n.state = "created" ON MATCH SET n.state = "matched"
```

`CREATE` and `MERGE` support single connected patterns and comma-separated pattern lists. Their node and relationship property maps accept scalar expressions evaluated against the current row, including parameters, variables, property access, and arithmetic. `MERGE` reuses the evaluated properties for both lookup and creation. Unconstrained new MERGE nodes without a label or property map are rejected. `SET` supports comma-separated property assignments with scalar expressions on the right-hand side, `SET n += {key: value}` map merges, and `SET n = {key: value}` map replacement. Null map values remove properties. `REMOVE` supports comma-separated property removal and node-label removal. `DELETE` and `DETACH DELETE` support comma-separated node and relationship variables; node deletion removes incident relationships before deleting the node.

Parameterized execution is available through the C API:

```c
ng_parameter params[1];
params[0].name = "name";
params[0].value.type = NG_VALUE_STRING;
params[0].value.length = 5;
params[0].value.as.string = "Alice";

ng_query_execute_params(g,
    "MATCH (a:Person) WHERE a.name = $name RETURN a",
    params, 1, stdout, NULL);
```

Named parameters use `$name` syntax and can appear anywhere scalar expressions are accepted: `WHERE`, property maps, `RETURN`, `WITH`, `SET`, `CREATE`, and `MERGE`. Missing parameters return a query error; extra supplied parameters are ignored.

Important MiniCypher limitations:

* It is not a full Cypher parser.
* `ORDER BY` uses strict post-projection scope for `WITH`; hidden projection visibility is not implemented.
* Map literals support nested maps and row-dependent scalar values in `WITH`, `RETURN`, `SET`, `CREATE`, and `MERGE`. Map values are printed in insertion order and are persisted in native snapshots.
* Lists and complex values are printed and compared for equality, but are not meaningfully ordered.
* Path bindings are supported for read patterns using `p=(a)-[r:TYPE]->(b)` and bounded variable-length patterns such as `p=(a)-[*1..3]->(b)`. `nodes(p)` and `relationships(p)` return typed ID lists; direct path projection returns a structured `{nodes: [...], relationships: [...]}` value. Path values are not valid write targets.
* `ON CREATE` and `ON MATCH` currently support `SET` property assignments and map updates after generic `MERGE`; subqueries are not implemented. The built-in `randomWalk` procedure and graph-registered procedures using `CALL ... YIELD field [AS alias]` are supported. Procedure result names must be unique, scalar results must contain valid values, and node/relationship results must reference existing records.
* `UNION`, `UNION ALL`, and `UNION DISTINCT` validate explicit branch column metadata independently of emitted rows. Names must agree, known non-numeric types must agree, and integer/double columns are compatible; empty and null-only branches are supported. Writes in UNION branches share one transaction and roll back together if a later branch fails.
* `UNWIND` currently expands scalar list literals, list-valued parameters, and list-valued properties.

Example:

```sh
./build/nautylus query graph.ng 'MATCH (n) RETURN n LIMIT 10'
./build/nautylus query graph.ng 'MATCH (n) RETURN n.name' --format verbose
./build/nautylus query graph.ng 'MATCH (n) RETURN n.name' --format plain
./build/nautylus query graph.ng 'MATCH (n) RETURN n.name' --format json
./build/nautylus explain 'MATCH (n:Person) WHERE n.name = "Alice" RETURN n'
./build/nautylus query graph.ng 'MATCH (n)-[:KNOWS]->(m) RETURN m'
./build/nautylus query graph.ng 'MATCH (n)-[:KNOWS*1..3]->(m) RETURN m'
./build/nautylus query graph.ng 'MATCH (n:Person) WHERE id(n) = 1 RETURN n.name'
./build/nautylus query graph.ng 'MATCH (n:Person) RETURN n.name ORDER BY n.name DESC LIMIT 5'
./build/nautylus query graph.ng 'MATCH (n:Person) RETURN n.city, count(n)'
./build/nautylus query graph.ng 'CREATE (a:Person {name: "Joe"})-[:KNOWS]->(b:Person {name: "Bob"}) RETURN a.name, b.name'
```

## Web Workbench

Start the local web interface:

```sh
./build/nautylus serve graph.ng 6180
```

Open `http://127.0.0.1:6180`. The workbench serves static assets from `resources/web` and `resources/logo`, and operates on the database path passed to `serve`.

The current workbench supports stats, MiniCypher query/explain, triple TSV import, sample graph creation, required/unique node-property constraints, exact-match index metadata, interactive graph rendering, a top query input, and a right-side node information panel with editable colors and typed node/relationship properties.

## File Formats

### Triple TSV

Triple import uses three tab-separated fields per line:

```text
source<TAB>relationship_type<TAB>target
```

Example:

```text
alice	KNOWS	bob
bob	WORKS_AT	acme
```

Duplicate suppression:

* duplicates are identified by `(source node, relationship type, target node)`;
* direction matters;
* existing relationships already in the database count;
* public C import calls can preserve parallel duplicates with `preserve_parallel != 0`;
* the current CLI uses duplicate suppression.

### Triple CSV

CSV triple import uses three comma-separated fields per line:

```text
source,relationship_type,target
```

Fields may be quoted with double quotes. Inside quoted fields, doubled quotes decode to one literal quote:

```csv
"ali,ce",KNOWS,"bo""b"
```

CSV import follows the same duplicate-suppression and rollback rules as TSV import. Embedded newlines inside quoted fields are not supported.

### Property Graph TSV

Node records:

```text
node<TAB>external_id<TAB>comma,separated,labels<TAB>key=value;key=value
```

Relationship records:

```text
relationship<TAB>external_id<TAB>source_external_id<TAB>type<TAB>target_external_id<TAB>key=value;key=value
```

Duplicate behavior:

* repeated node external IDs update the same logical node;
* labels are deduplicated per node;
* relationship duplicates are suppressed by `(source, type, target)` unless `preserve_parallel != 0` is used from C;
* relationship external IDs are parsed for format compatibility but are not currently stored as a user-visible property.

Property values use a compact typed encoding:

| Type | Encoding | Example |
| --- | --- | --- |
| null | `n` | `deleted=n` |
| bool | `b:0` or `b:1` | `active=b:1` |
| int64 | `i:<decimal>` | `age=i:42` |
| double | `d:<16 hex bits>` | `score=d:4004000000000000` |
| string | `s:<hex bytes>` | `name=s:416c696365` |
| bytes | `x:<hex bytes>` | `blob=x:0001ff` |

Text and encoding rules:

* CRLF and LF line endings are accepted.
* Blank lines and comments are not accepted.
* Empty labels and empty property keys are invalid.
* Labels, relationship types, and property keys must not contain tab, newline, carriage return, comma, semicolon, or equals.
* Triple entity names and relationship names must not contain tabs or newlines.
* Hex input accepts uppercase or lowercase digits.
* String values are hex-encoded bytes; the library stores a length, so embedded NUL bytes can round-trip through property-graph TSV.
* UTF-8 is not currently validated.
* Double values are encoded as exact 64-bit payloads, preserving signed zero and NaN bit patterns.

Null semantics:

* An absent property and a property present with `NG_VALUE_NULL` are different states.
* `key=n` imports a present null property.
* Setting a property to null does not delete it.
* Delete properties through `ng_node_unset()` and `ng_relationship_unset()`.

`__nautylus_external_id` is reserved for the property-graph importer. Export may include it for databases created from imports; reimport treats it as structural metadata instead of a user property.

## Persistence and Safety

The native database file is a single portable snapshot. Details are in [docs/snapshot-format.md](docs/snapshot-format.md).

Save sequence:

1. Validate the in-memory graph.
2. Encode the graph into a portable little-endian payload.
3. Write a versioned header and checksum to `FILE.tmp`.
4. Close the temporary file.
5. Rename the temporary file over the target path.

If validation, encoding, writing, or closing fails before the rename, the previous database file is left intact. Rename atomicity and crash durability depend on the operating system and filesystem; the current implementation does not fsync the containing directory.

Property-graph export writes two files. When replacing existing output files it stages temporary files and uses `.nautylusbak` backups to avoid knowingly leaving an old/new mismatch after ordinary rename errors. Existing `.nautylusbak` files block export with `NG_EXISTS`. Full crash recovery for every possible two-file rename interruption is not claimed yet.

## Determinism

Guaranteed by the current implementation and tests:

* Repeated property-graph exports of the same graph are byte-identical.
* Property-graph export order is sorted by node ID, relationship ID, label ID, and property key ID.
* Typed values preserve exact stored values, including double bit patterns.
* IDs are stable after save/reopen.

Not currently claimed:

* byte-identical native snapshots for independently built equivalent graphs;
* insertion-order-independent symbol IDs;
* deterministic output after changing symbol allocation order.

## Limitations

Not implemented yet:

* durable transaction journal;
* multi-process writer coordination;
* full Cypher compatibility;
* scoped subqueries;
* direct path rendering beyond the current structured path/list values;
* large-scale graph analytics;
* complete two-file export crash recovery;
* fuzzing and profiling harnesses.

## Development Status

Capability summary:

| Area | Implemented now | Remaining |
| --- | --- | --- |
| Core graph | CRUD, labels, typed properties, property deletion, directed relationships, validation | Incremental adjacency maintenance |
| Persistence | Single-file snapshots, checksum, strict load checks, atomic replacement where supported | Generations, per-section checksums, directory fsync, migrations |
| Query | Property retrieval, label checks, exact node scans, snapshot node indexes, persistent exact-match index metadata, persisted required/unique property constraints, property-aware node creation API, property-mutation constraint enforcement, bounded traversal, multi-node MiniCypher, `WHERE`, `WITH`, `UNWIND`, `OPTIONAL MATCH`, parameters, aggregates, `ORDER BY`, `SKIP`/`LIMIT`, `UNION`/`UNION ALL`/`UNION DISTINCT`, rollback-protected `CREATE`/`MERGE`/`SET`/`REMOVE`/`DELETE`/`DETACH DELETE`, nested map expressions, list expressions, searched `CASE`, fixed and bounded variable-length path bindings with `nodes()`/`relationships()`, generic `MERGE` `ON CREATE SET`/`ON MATCH SET`, typed graph-registered procedures with result aliases, seeded `randomWalk` procedure | Full Cypher compatibility, subqueries |
| Analytics | Degree centrality, PageRank, eigenvector, closeness, harmonic centrality, weak/strong components, triangle count, local clustering coefficient, articulation points, bridges, common-neighbor, Adamic-Adar, Resource Allocation, topological sort, seeded random walks, weighted Dijkstra, BFS, DFS path enumeration, A*, minimum spanning tree, maximum flow, label propagation, Louvain-style local moving, FastRP, Node2Vec-style embeddings, GraphSAGE inference/training, exact/approximate/flat-ANN/HNSW vector search, Jaccard KNN, and label-filtered KNN | Multilevel Louvain/Leiden aggregation, richer filtered similarity, scalable implementations |
| Import/export | Triple TSV/CSV, property-graph TSV, CLI workflows, rollback on import failure | Stronger two-file crash recovery, richer CLI flags |
| Release quality | Strict C99 tests, ASan/UBSan run with LeakSanitizer disabled in this environment, documented tested limits, small local performance baseline, local web workbench smoke coverage | CI, fuzzing, profiling |

Detailed evidence is in [STATUS.md](STATUS.md).

## Project Documents

* [STATUS.md](STATUS.md): implementation evidence and roadmap status.
* [docs/api.md](docs/api.md): C API semantics and ownership rules.
* [docs/snapshot-format.md](docs/snapshot-format.md): native snapshot format and compatibility policy.
* [docs/limits.md](docs/limits.md): tested limits and local performance baseline.
* [docs/examples.md](docs/examples.md): runnable examples and query gallery.
* [docs/graphsage.md](docs/graphsage.md): GraphSAGE, training, prediction, and vector-search APIs.
* [docs/bindings.md](docs/bindings.md): Python and PHP FFI bindings.
* [src/nautylus.h](src/nautylus.h): public C API.
* [src/nautylus.c](src/nautylus.c): core library implementation.
* [src/nautylus.c99main.c](src/nautylus.c99main.c): CLI entry point.
* [resources/web/index.html](resources/web/index.html): local web workbench.
* [tests/test_nautylus.c](tests/test_nautylus.c): regression suite.
* [examples/basic.c](examples/basic.c): minimal embeddable C API example.
* [LICENSE](LICENSE): MIT license.
* `agent.md`: full project specification and roadmap.

Planned project files:

* `CONTRIBUTING.md`
* `CHANGELOG.md`
* `SECURITY.md`

# Small C99 Property Graph Database

> Historical design/specification note: this file describes the original target
> and engineering constraints. For current implementation status, use
> `STATUS.md`; for runnable examples, use `docs/examples.md` and `examples/`.

## Goal

Replace the current causal language-model experiment with a focused, embedded property-graph database written in portable C99.

The library should provide a small, inspectable subset of Neo4j-style functionality:

* Labeled nodes.
* Typed, directed relationships.
* Properties on nodes and relationships.
* Deterministic graph import.
* Exact-match indexes.
* Pattern matching and graph traversal.
* A compact Cypher-inspired query language.
* Portable database files.
* Useful command-line tools.

The implementation should prioritize correctness, deterministic behavior, portability, and ease of inspection over feature completeness or large-scale performance.

The project is Neo4j-inspired but is not intended to provide full Neo4j, Bolt, or Cypher compatibility.

## Current deployable slice

As of the current implementation, the repository builds a portable C99 library and `nautylus` CLI with:

* Native create/open/save/validate support using a portable little-endian snapshot file.
* Stable internal IDs for symbols, nodes, and relationships across save/reopen.
* Labeled nodes, typed directed relationships, and typed node/relationship properties.
* Relationship enumeration, bounded breadth-first traversal, label checks, property retrieval, property-aware node creation, and exact node scans by label/property.
* A practical MiniCypher subset with multi-node `MATCH`, relationship expansion, bounded path bindings, `WHERE`, `WITH`, `UNWIND`, `OPTIONAL MATCH`, parameters, aggregates, ordering, `UNION`, procedures, transactional writes, and `EXPLAIN`.
* Triple TSV import/export and property-graph TSV import/export.
* CLI commands for creation, validation, stats, import/export, constraints, index metadata, querying, explaining, benchmarking, and the local web workbench.
* Analytics APIs for centrality, components, communities, paths, flows, link prediction, similarity, embeddings, GraphSAGE training/inference, and vector search.
* Regression coverage for rollback on import/query failure, public in-memory transactions, snapshot node indexes, constraints, MiniCypher reads/writes, typed property-graph round trips, strict snapshot corruption checks, deterministic exports, guarded export replacement, CLI workflows, the local web workbench, analytics, GraphSAGE, and vector indexes.
* User-facing documentation in `README.md`, `STATUS.md`, `docs/`, and `examples/`.

The remaining roadmap items include scoped subqueries, stronger two-file export commit recovery, durable transaction journaling, expanded malformed-record coverage, fuzzing/profiling, CI, richer analytics, and broader HTTP/API/web work.

---

## Language and portability requirements

The implementation must:

* Conform to ISO C99.
* Avoid C11 atomics, threads, `_Generic`, and other post-C99 features.
* Compile with GCC, Clang, and other conforming C99 compilers.
* Build cleanly with:

```sh
cc -std=c99 -Wall -Wextra -Wpedantic
```

* Avoid compiler-specific extensions in the core library.
* Use fixed-width integer types from `<stdint.h>`.
* Check integer overflow before allocation and offset arithmetic.
* Use explicit byte-order conversion for persistent numeric fields.
* Avoid serializing raw C structures directly.
* Support sanitizer builds where the compiler provides them.

Optional platform-specific acceleration may be added behind compile-time feature flags, but the default build must remain portable C99.

---

## Scope

### Initially supported operations

* Create, open, close, and validate a graph database.
* Create labeled nodes.
* Create typed, directed relationships.
* Store properties on nodes and relationships.
* Retrieve nodes and relationships by stable internal ID.
* Delete nodes and relationships.
* Enumerate incoming and outgoing relationships.
* Match graph patterns.
* Filter matches by labels, relationship types, IDs, and property values.
* Traverse the graph with configurable depth and direction.
* Create exact-match property indexes.
* Import triples and property-graph records.
* Export graph data in readable and machine-processable formats.
* Execute a limited Cypher-inspired query language.
* Save the database portably and reopen it without changing IDs.
* Run read-only consistency checks.

### Explicit non-goals for the first version

* Full Cypher compatibility.
* Bolt protocol compatibility.
* Neo4j database-file compatibility.
* Distributed storage or clustering.
* Multi-process concurrent writers.
* Lock-free concurrency.
* General full-text search.
* Full distributed approximate-nearest-neighbor services.
* GAT training or dependency-heavy ML runtimes.
* External plugin loading.
* Autonomous natural-language question answering.

---

## Core graph model

The database uses a directed property-graph model.

### Node

Each node contains:

* A stable unsigned 64-bit node ID.
* Zero or more labels.
* Zero or more key-value properties.
* References to incoming and outgoing relationships.

Conceptually:

```text
Node {
    id
    labels[]
    properties{}
}
```

Example:

```text
(:Person {
    name: "Alice",
    age: 31
})
```

### Relationship

Each relationship contains:

* A stable unsigned 64-bit relationship ID.
* A source node ID.
* A target node ID.
* Exactly one relationship type.
* Zero or more key-value properties.

Conceptually:

```text
Relationship {
    id
    source
    target
    type
    properties{}
}
```

Example:

```text
(alice)-[:WORKS_AT {since: 2022}]->(acme)
```

Relationships are always directed. Query operations may explicitly request outgoing, incoming, or either direction.

### Symbols

Labels, relationship types, and property keys are interned in deterministic symbol dictionaries.

Each symbol has:

* A stable numeric ID.
* An original UTF-8 string.
* A symbol category.
* A stored string length.

Symbol categories must remain distinct. A node label and relationship type with the same text must not be confused internally.

---

## Property types

The first version should support:

* Null.
* Boolean.
* Signed 64-bit integer.
* IEEE 754 double.
* UTF-8 string.
* Byte string.

Optional later types may include:

* Lists.
* Date and time values.
* Spatial values.

Properties must use an explicitly tagged representation. Code must never infer the property type from untagged memory.

Example public representation:

```c
typedef enum ng_value_type {
    NG_VALUE_NULL = 0,
    NG_VALUE_BOOL,
    NG_VALUE_INT64,
    NG_VALUE_DOUBLE,
    NG_VALUE_STRING,
    NG_VALUE_BYTES
} ng_value_type;
```

Strings and byte values must carry explicit lengths and must not depend on null termination inside the storage engine.

---

## Identifiers

The database must expose stable IDs for:

* Nodes.
* Relationships.
* Labels.
* Relationship types.
* Property keys.

IDs must remain stable after:

* Reopening the database.
* Adding new nodes or relationships.
* Creating indexes.
* Running read-only validation.
* Exporting and importing a native database snapshot.

Deleted IDs must not be silently assigned to unrelated objects during the same database generation.

The implementation may use monotonically increasing IDs initially. Free-list reuse may be added later if stale-reference detection is provided.

ID `0` should be reserved as an invalid or absent identifier.

---

## Input formats

### Triple import

The basic triple format uses one record per line:

```text
head<TAB>relation<TAB>tail
```

Example:

```text
alice	works_at	acme
acme	located_in	berlin
```

The importer should create:

* One node for each unique entity string.
* One relationship type for each unique relation string.
* One directed relationship for each unique triple.

An optional fourth field may provide an external record ID:

```text
document_id<TAB>head<TAB>relation<TAB>tail
```

The external ID should be stored as import metadata or as a configurable property.

### Property-graph import

A second import format should support explicit node and relationship records.

Node example:

```text
node<TAB>external_id<TAB>labels<TAB>properties
```

Relationship example:

```text
relationship<TAB>external_id<TAB>source_id<TAB>type<TAB>target_id<TAB>properties
```

Labels may use a documented delimiter. Properties may use a restricted JSON object or another precisely specified encoding.

### Loader requirements

The loader must:

* Assign deterministic IDs for identical input.
* Reject malformed records with line-number diagnostics.
* Handle CRLF and LF line endings.
* Detect overlong records safely.
* Reject embedded null bytes where text is required.
* Validate UTF-8 when UTF-8 validation is enabled.
* Support escaped delimiters where the format permits them.
* Support duplicate nodes without creating duplicate logical entities.
* Support duplicate relationships without double-counting them by default.
* Offer an explicit option to preserve parallel duplicate relationships.
* Avoid partially applying a malformed batch.
* Report duplicate, skipped, and rejected record counts.

Deterministic import must not depend on hash-table iteration order.

---

## Storage architecture

The database should use a portable, explicitly versioned file format.

A database may be represented as:

```text
graph.ng/
    manifest
    nodes.dat
    relationships.dat
    properties.dat
    strings.dat
    symbols.dat
    adjacency.dat
    indexes/
```

A single-file representation may be added later.

### Storage requirements

Persistent files must contain:

* Magic bytes.
* Format version.
* Feature flags.
* Section lengths.
* Record counts.
* Checksums.
* Database generation ID.
* Clean-shutdown or commit marker.

The implementation must not write native pointers, `size_t`, enum layouts, structure padding, or host-endian structures directly to disk.

Every field must use:

* A defined integer width.
* A defined byte order.
* A documented encoded representation.

### Atomic persistence

A mutating operation or transaction must not leave the database appearing valid when only part of the change reached durable storage.

The first version may use one of these approaches:

1. Write a complete temporary snapshot and atomically rename it.
2. Use a small write-ahead journal with commit records.
3. Use append-only generation files and atomically replace the manifest.

The selected approach must document its guarantees for:

* Process crashes.
* Operating-system crashes.
* Partial writes.
* Failed disk-full operations.
* Interrupted imports.

---

## In-memory representation

The initial in-memory implementation may use:

* Dynamic arrays for node and relationship records.
* Open-addressed hash tables for symbol and external-ID lookup.
* CSR-like adjacency arrays for compact traversal.
* Sorted property arrays for small property sets.
* Append-only string storage.

Public APIs must not expose internal pointers whose validity changes after graph mutation.

### Adjacency

The graph must support efficient access to:

* Outgoing relationships for a node.
* Incoming relationships for a node.
* Relationships filtered by type.
* Neighboring nodes.

A compact representation may use separate incoming and outgoing adjacency structures.

Each adjacency entry must reference a relationship ID. Relationship type and endpoints must be read from the canonical relationship record rather than duplicated inconsistently.

Graph construction must validate:

* Node bounds.
* Relationship bounds.
* Source and target existence.
* Relationship type IDs.
* Adjacency offsets.
* Monotonic offset arrays.
* Integer overflow in edge counts and allocations.

---

## Mutation API

The C library should provide operations resembling:

```c
ng_status ng_node_create(
    ng_graph *graph,
    const ng_label_id *labels,
    size_t label_count,
    ng_node_id *out_node
);

ng_status ng_relationship_create(
    ng_graph *graph,
    ng_node_id source,
    ng_relation_type_id type,
    ng_node_id target,
    ng_relationship_id *out_relationship
);

ng_status ng_node_set_property(
    ng_graph *graph,
    ng_node_id node,
    ng_property_key_id key,
    const ng_value *value
);

ng_status ng_relationship_set_property(
    ng_graph *graph,
    ng_relationship_id relationship,
    ng_property_key_id key,
    const ng_value *value
);
```

All API operations must return explicit status values.

Expected status categories include:

* Success.
* Invalid argument.
* Not found.
* Already exists.
* Parse error.
* Type mismatch.
* Constraint violation.
* Out of memory.
* I/O error.
* Corrupt database.
* Unsupported format.
* Incompatible version.
* Busy or transaction conflict.

The library must not use `exit`, print directly to standard output, or terminate the hosting process.

---

## Transactions

The first version should provide small single-writer transactions.

### Required transaction behavior

* At most one active writer per graph handle.
* Multiple read operations may occur when no mutation invalidates their iterators.
* A writer can commit or roll back.
* Uncommitted changes must not survive rollback.
* Failed operations must not leave half-created graph objects.
* Commit must either publish the complete transaction or report failure.
* IDs allocated in a rolled-back transaction may remain unused.

Example API:

```c
ng_status ng_tx_begin(ng_graph *graph, ng_tx **out_tx);
ng_status ng_tx_commit(ng_tx *tx);
void ng_tx_rollback(ng_tx *tx);
```

Thread safety is not required initially. The documentation must state that a graph handle must not be used concurrently unless an optional synchronization layer is enabled.

---

## Indexes and constraints

### Exact-match property indexes

The first release should support indexes of the form:

```text
(label, property_key, property_value) -> node IDs
```

An optional relationship index may support:

```text
(relationship_type, property_key, property_value) -> relationship IDs
```

Indexes must support:

* String values.
* Signed integer values.
* Boolean values.
* Exact double-bit-pattern values, or clearly documented numeric comparison rules.

Index files must include:

* Index type.
* Target label or relationship type.
* Property key.
* Value encoding.
* Database generation.
* Checksum.

Indexes may be rebuilt from canonical graph records.

### Initial constraints

The first version may support:

* Unique node property within a label.
* Required node property for a label.
* Required relationship property for a type.

Constraint checks must occur before committing a transaction.

---

## Query language

Provide a deliberately small Cypher-inspired language named, for example, **MiniCypher**.

It must not be described as fully Cypher-compatible.

### Initial syntax

#### Match nodes

```text
MATCH (n)
RETURN n
```

```text
MATCH (n:Person)
RETURN n
```

#### Match directed relationships

```text
MATCH (a)-[r:WORKS_AT]->(b)
RETURN a, r, b
```

#### Match incoming relationships

```text
MATCH (a)<-[r:WORKS_AT]-(b)
RETURN a, b
```

#### Match either direction

```text
MATCH (a)-[r:KNOWS]-(b)
RETURN a, b
```

#### Property predicates

```text
MATCH (n:Person)
WHERE n.name = "Alice"
RETURN n
```

```text
MATCH (a:Person)-[:WORKS_AT]->(b:Company)
WHERE b.name = "Acme"
RETURN a.name
```

#### ID predicates

```text
MATCH (n)
WHERE id(n) = 42
RETURN n
```

#### Limits

```text
MATCH (n:Person)
RETURN n.name
LIMIT 10
```

### Mutation syntax

The current implementation supports rollback-protected mutation queries such as:

```text
CREATE (:Person {name: "Alice"})
```

```text
MATCH (a:Person), (b:Company)
WHERE a.name = "Alice" AND b.name = "Acme"
CREATE (a)-[:WORKS_AT]->(b)
```

The supported write subset includes `CREATE`, `MERGE`, scalar and map-based
`SET`, `REMOVE`, `DELETE`, `DETACH DELETE`, comma-separated pattern lists, and
`MERGE` `ON CREATE SET` / `ON MATCH SET` property updates.

### Explicit query limitations

The current parser still does not claim:

* Subqueries.
* Full Neo4j/Cypher compatibility.
* Direct path rendering polish beyond current path/list values.
* Query-plan hints.
* Full Cypher null semantics in every edge case.

Unsupported syntax must produce a clear diagnostic with line and column numbers.

---

## Query execution

The query engine should use a simple iterator-based execution model.

Possible operators include:

* Node scan.
* Label scan.
* Property-index lookup.
* Relationship expansion.
* Directional expansion.
* Type filter.
* Property filter.
* Projection.
* Limit.

A query plan can be represented as a small tree or linear pipeline.

Example:

```text
PropertyIndexLookup(Person, name, "Alice")
    -> ExpandOutgoing(WORKS_AT)
    -> Project(target.name)
    -> Limit(10)
```

The executor must:

* Produce deterministic result ordering unless otherwise documented.
* Avoid returning deleted records.
* Check iterator invalidation.
* Apply result limits before unbounded memory accumulation.
* Allow callers to stop iteration early.
* Return typed values rather than formatted strings.

An optional `EXPLAIN` command should print the selected execution operators.

---

## Traversal API

Provide a programmatic graph traversal API independent of the query parser.

Supported traversal controls should include:

* Starting node.
* Incoming, outgoing, or either direction.
* Optional relationship-type filter.
* Minimum depth.
* Maximum depth.
* Breadth-first or depth-first order.
* Node uniqueness policy.
* Relationship uniqueness policy.
* Early-stop callback.
* Maximum visited-node count.

Example:

```c
typedef struct ng_traversal_options {
    ng_direction direction;
    const ng_relation_type_id *types;
    size_t type_count;
    uint32_t min_depth;
    uint32_t max_depth;
    ng_traversal_order order;
    uint64_t visit_limit;
} ng_traversal_options;
```

Traversal must guard against:

* Cycles.
* Depth overflow.
* Visit-count overflow.
* Invalid relationship endpoints.
* Unbounded allocation caused by corrupt input.

---

## Triple-oriented queries

Historical design sketch: direct triple lookup commands were considered for
knowledge-graph workflows. The current CLI uses `query`, `search`, `export`, and
the C APIs for these lookups; the `neighbors` and `exists` command forms below
are not current CLI commands.

### Find tails

```sh
./nautylus neighbors \
  --head alice \
  --relation works_at \
  --direction outgoing
```

Expected output:

```text
NODE_ID	ENTITY	RELATIONSHIP_ID
18	acme	93
```

### Find heads

```sh
./nautylus neighbors \
  --tail berlin \
  --relation located_in \
  --direction incoming
```

### Check a fact

```sh
./nautylus exists \
  --head alice \
  --relation works_at \
  --tail acme
```

These command sketches perform exact graph lookup. Link-prediction basics are
now available through the C analytics API.

---

## Command-line interface

### Create a database

```sh
./nautylus create \
  --database graph.ng
```

### Import triples

```sh
./nautylus import \
  --database graph.ng \
  --triples data.tsv \
  --delimiter tab \
  --deduplicate
```

### Import property-graph data

```sh
./nautylus import \
  --database graph.ng \
  --nodes nodes.tsv \
  --relationships relationships.tsv
```

### Run a query

```sh
./nautylus query \
  --database graph.ng \
  --query 'MATCH (a:Person)-[:WORKS_AT]->(b) RETURN a.name, b.name'
```

### Run a query from a file

```sh
./nautylus query \
  --database graph.ng \
  --file query.mcypher
```

### Interactive shell

```sh
./nautylus shell \
  --database graph.ng
```

### Show graph statistics

```sh
./nautylus stats \
  --database graph.ng
```

Example statistics:

```text
nodes: 1024
relationships: 4917
labels: 8
relationship types: 21
property keys: 34
property values: 12883
indexes: 3
database generation: 17
```

### Validate storage

```sh
./nautylus check \
  --database graph.ng
```

### Export triples

```sh
./nautylus export \
  --database graph.ng \
  --format triples \
  --output graph.tsv
```

### Explain a query

```sh
./nautylus explain \
  --database graph.ng \
  --query 'MATCH (n:Person) WHERE n.name = "Alice" RETURN n'
```

---

## Determinism

Given:

* The same input bytes.
* The same import options.
* The same database format version.
* The same configured seed where randomized testing is used.

The implementation must produce:

* The same node IDs.
* The same relationship IDs.
* The same symbol IDs.
* The same duplicate handling.
* The same query result order.
* The same exported logical records.

Persistent file bytes should also be deterministic where practical. Values such as timestamps or random database UUIDs must be disabled, normalized, or explicitly excluded from deterministic-build guarantees.

Hash randomization must not affect externally visible IDs or output ordering.

---

## Database format

The native format must contain:

* Magic number.
* Major and minor format version.
* Feature flags.
* Database generation ID.
* Record counts.
* Symbol dictionaries.
* Node records.
* Relationship records.
* Property records.
* String and byte storage.
* Incoming and outgoing adjacency metadata.
* Index metadata.
* Constraint metadata.
* Checksums.
* Transaction or clean-shutdown state.

Loading must reject:

* Invalid magic bytes.
* Unsupported versions.
* Invalid checksums.
* Truncated sections.
* Overlapping sections.
* Impossible record counts.
* Duplicate required sections.
* Unknown mandatory feature flags.
* Invalid node or relationship IDs.
* Relationships referencing absent nodes.
* Invalid symbol references.
* Invalid property value tags.
* Out-of-range string offsets.
* Non-monotonic adjacency offsets.
* Indexes built for a different database generation.

Unknown optional sections may be skipped only when their lengths and compatibility rules are valid.

---

## Checksums

Each persistent section should have its own checksum. The manifest should also include a checksum covering section metadata.

A simple initial implementation may use CRC32C or another documented non-cryptographic corruption checksum.

Checksums are intended to detect accidental corruption. They are not authentication and must not be presented as protection against malicious modification.

A future authenticated format may add a cryptographic hash or message-authentication code.

---

## Resource limits and security

All parsers and loaders must support configurable limits for:

* Maximum line length.
* Maximum identifier length.
* Maximum property-key length.
* Maximum string-property length.
* Maximum labels per node.
* Maximum properties per object.
* Maximum nodes.
* Maximum relationships.
* Maximum query length.
* Maximum parser nesting.
* Maximum traversal depth.
* Maximum visited objects.
* Maximum returned rows.
* Maximum temporary memory.

The implementation must:

* Check multiplication and addition before allocating memory.
* Reject impossible file offsets.
* Avoid recursion for untrusted graph depth where practical.
* Avoid quadratic behavior for common malformed inputs.
* Never use unchecked `strcpy`, `strcat`, or `sprintf`.
* Use length-aware parsing.
* Clean up correctly after allocation failure.
* Avoid undefined behavior when reading malformed files.
* Treat imported data and database files as untrusted.

The documentation must clearly distinguish:

* Corruption detection.
* Crash consistency.
* Access control.
* Encryption.
* Protection against malicious files.

The initial release does not need to provide authentication, authorization, or encryption.

---

## Public library structure

A possible source layout is:

```text
include/
    nautylus.h
    nautylus_value.h
    nautylus_query.h
    nautylus_traversal.h

src/
    nautylus.c
    graph.c
    node.c
    relationship.c
    property.c
    symbol.c
    string_store.c
    adjacency.c
    hash_table.c
    index.c
    transaction.c
    storage.c
    checksum.c
    import.c
    export.c
    lexer.c
    parser.c
    planner.c
    executor.c
    traversal.c
    diagnostics.c

cli/
    main.c
    command_create.c
    command_import.c
    command_query.c
    command_shell.c
    command_export.c
    command_check.c
    command_stats.c

tests/
    test_import.c
    test_graph.c
    test_properties.c
    test_traversal.c
    test_query_parser.c
    test_query_executor.c
    test_storage.c
    test_transactions.c
    test_corruption.c
    test_determinism.c
```

The core library should not depend on the CLI.

---

## Testing strategy

### Unit tests

Test:

* Symbol interning.
* Node creation and deletion.
* Relationship creation and deletion.
* Property encoding.
* String storage.
* Hash-table growth.
* Adjacency construction.
* Incoming and outgoing expansion.
* Index lookup.
* Constraint enforcement.
* Lexer behavior.
* Parser diagnostics.
* Query execution.
* Traversal cycle handling.
* Transaction rollback.
* Checksum validation.

### Determinism tests

Verify that repeated imports produce identical:

* IDs.
* Dictionaries.
* Query results.
* Exports.
* Persistent snapshots where deterministic serialization is promised.

### Corruption tests

Modify or truncate:

* Manifest fields.
* Record counts.
* Section offsets.
* Checksums.
* Node IDs.
* Relationship endpoints.
* Property tags.
* String offsets.
* Adjacency offsets.
* Index generation values.

Every corrupted fixture must either be rejected safely or handled according to documented recovery rules.

### Failure-injection tests

Simulate failure during:

* Allocation.
* File creation.
* File writing.
* File synchronization.
* Manifest replacement.
* Transaction commit.
* Index construction.

The graph must remain valid or be recoverable after each injected failure.

### Fuzz testing

Fuzz:

* Triple parsing.
* Property parsing.
* Query lexing.
* Query parsing.
* Database loading.
* Manifest validation.

Fuzz targets must accept arbitrary byte sequences without crashing, hanging, or performing unbounded allocations.

### Sanitizer testing

Where supported, run:

```sh
-fsanitize=address,undefined
```

Valgrind or an equivalent memory checker may also be used.

---

## Implementation milestones

### Milestone 1 — C99 graph foundation

* [x] Establish a strict C99 build.
* [x] Define public IDs, values, and statuses.
* [ ] Define allocator interfaces.
* [x] Implement dynamic arrays and checked allocation helpers.
* [x] Implement deterministic symbol dictionaries.
* [x] Implement node and relationship creation.
* [x] Add duplicate and malformed-record tests.
* [x] Add a dedicated `nautylus` executable.

### Milestone 2 — Graph representation

* [x] Implement directed, typed relationships.
* [x] Implement incoming and outgoing adjacency.
* [x] Validate all graph IDs and bounds.
* [x] Implement labels and typed properties.
* [x] Add graph consistency validation.
* [x] Add traversal tests covering cycles and parallel edges.

### Milestone 3 — Import and export

* [x] Implement streaming TSV triple loading.
* [x] Implement CSV triple loading.
* [x] Implement property-graph node and relationship loading.
* [x] Add deterministic ID assignment.
* [x] Add configurable duplicate handling.
* [x] Add line-number and column diagnostics.
* [x] Implement triple and property-graph export.

### Milestone 4 — Persistent storage

* [x] Define the portable versioned file format.
* [x] Serialize dictionaries, records, and properties.
* [ ] Serialize adjacency.
* [ ] Add per-section checksums.
* [x] Implement atomic snapshot commits where filesystem rename is atomic.
* [ ] Implement journal commits.
* [x] Reject incompatible or corrupt files.
* [x] Verify IDs and query results before and after reopening.

### Milestone 5 — Query engine

* [x] Implement the MiniCypher lexer.
* [x] Implement node and relationship pattern parsing.
* [x] Implement property and ID predicates.
* [x] Implement scans, bounded multi-hop expansion, filtering, single-property projection, multi-column projection, and limits.
* [x] Add deterministic query-result tests.
* [ ] Add line-and-column parser diagnostics.
* [x] Implement `EXPLAIN`.

### Milestone 6 — Transactions and indexes

* [x] Implement single-writer transactions.
* [x] Implement commit and rollback.
* [x] Implement exact-match node property indexes.
* [x] Add persisted uniqueness and required-property constraints.
* [x] Add index rebuilding.
* [ ] Add commit-failure and recovery tests.

### Milestone 7 — Release quality

* [ ] Add phase-level profiling.
* [ ] Add configurable resource limits.
* [ ] Add large node and relationship count stress tests.
* [x] Add allocation-failure testing.
* [ ] Add parser and storage fuzz targets.
* [ ] Run normal, sanitizer, and memory-checker test suites.
* [x] Document supported syntax and formats.
* [ ] Document supported limits and security assumptions.


### Milestone 8 — Web server and graph interface

* [ ] Implement an optional HTTP server for opening and querying a local graph database.
* [ ] Provide a browser-based query workspace inspired by Neo4j Browser.
* [ ] Visualize query results as interactive graphs using D3.js.
* [ ] Support tabular, JSON, and graph result views.
* [ ] Add request limits, read-only mode, authentication hooks, and server security tests.

#### Server architecture

Add a dedicated executable:

```sh
./nautylus-server \
  --database graph.ng \
  --bind 127.0.0.1 \
  --port 7474 \
  --web-root web/
```

The server should reuse the existing graph, transaction, query-parser, planner, and executor modules. Query behavior exposed through HTTP must match the behavior of the `nautylus query` command.

The server must not contain a separate graph-query implementation.

A possible source layout is:

```text
server/
    main.c
    http_server.c
    http_parser.c
    http_response.c
    api.c
    api_query.c
    api_schema.c
    api_stats.c
    api_node.c
    api_relationship.c
    static_files.c
    json_writer.c
    server_config.c
    server_limits.c

web/
    index.html
    app.js
    api.js
    graph.js
    table.js
    query-editor.js
    history.js
    styles.css
    vendor/
        d3.min.js
```

The HTTP server and JSON API must remain optional. The core `nautylus` library must not depend on HTTP, JavaScript, D3.js, or browser-specific components.

#### C99 server requirements

The server implementation must:

* Remain compatible with ISO C99.
* Use the public `nautylus` library API.
* Avoid C11 threads and atomics.
* Support a simple single-threaded event loop initially.
* Support a configurable maximum number of connections.
* Reject oversized request headers and bodies.
* Apply configurable query execution limits.
* Handle partial socket reads and writes.
* Handle disconnected clients without terminating the server.
* Avoid blocking indefinitely on incomplete requests.
* Return structured errors instead of HTML error pages for API requests.
* Shut down cleanly on supported operating systems.
* Never expose internal pointers, file paths, or process memory in error responses.

The first version may use a small embedded HTTP dependency if it is:

* Compatible with C99.
* Permissively licensed.
* Auditable and easy to vendor.
* Optional at compile time.
* Used only by the server executable.

A minimal built-in HTTP/1.1 implementation is also acceptable if its supported behavior is documented precisely.

The first version does not need:

* HTTP/2.
* HTTP/3.
* WebSockets.
* TLS termination.
* Multiple worker processes.
* Distributed sessions.
* General-purpose file hosting.

TLS should normally be provided by a reverse proxy when remote access is required.

#### Server modes

The server should support two operating modes.

##### Read-only mode

```sh
./nautylus-server \
  --database graph.ng \
  --read-only
```

Read-only mode must reject:

* Mutation queries.
* Import operations.
* Index creation.
* Constraint changes.
* Database replacement.
* Administrative write operations.

Read-only mode should be the default when the server is bound to an address other than loopback.

##### Read-write mode

```sh
./nautylus-server \
  --database graph.ng \
  --allow-writes
```

Read-write mode may execute supported mutation queries and administrative operations through the existing single-writer transaction system.

Write requests must:

* Use explicit transactions.
* Commit atomically.
* Return the resulting database generation.
* Roll back automatically when parsing or execution fails.
* Respect all constraints and resource limits.
* Reject concurrent write transactions with a clear conflict response.

#### HTTP API

All API responses should use UTF-8 JSON.

The initial API prefix should be versioned:

```text
/api/v1/
```

##### Health endpoint

```http
GET /api/v1/health
```

Example response:

```json
{
  "status": "ok",
  "database_open": true,
  "read_only": true,
  "format_version": 1
}
```

##### Database statistics

```http
GET /api/v1/stats
```

Example response:

```json
{
  "nodes": 1024,
  "relationships": 4917,
  "labels": 8,
  "relationship_types": 21,
  "property_keys": 34,
  "indexes": 3,
  "generation": 17
}
```

##### Schema information

```http
GET /api/v1/schema
```

The schema response should include:

* Node labels.
* Relationship types.
* Property keys.
* Property types observed for each label or relationship type.
* Defined indexes.
* Defined constraints.

##### Execute a query

```http
POST /api/v1/query
Content-Type: application/json
```

Request:

```json
{
  "query": "MATCH (a:Person)-[r:WORKS_AT]->(b:Company) RETURN a, r, b LIMIT 100",
  "parameters": {},
  "result_format": "graph"
}
```

Supported result formats should include:

* `graph`
* `table`
* `json`

A query request may additionally provide:

```json
{
  "max_rows": 1000,
  "max_nodes": 500,
  "max_relationships": 1000,
  "timeout_ms": 5000
}
```

Client-provided values may only reduce server-configured limits. They must not increase them.

##### Query response

A successful response should contain:

```json
{
  "columns": ["a", "r", "b"],
  "rows": [],
  "graph": {
    "nodes": [],
    "relationships": []
  },
  "summary": {
    "rows_returned": 1,
    "nodes_returned": 2,
    "relationships_returned": 1,
    "execution_time_ms": 2,
    "database_generation": 17,
    "truncated": false
  }
}
```

Graph nodes should use a representation similar to:

```json
{
  "id": "42",
  "labels": ["Person"],
  "properties": {
    "name": "Alice",
    "age": 31
  }
}
```

Graph relationships should use:

```json
{
  "id": "93",
  "source": "42",
  "target": "18",
  "type": "WORKS_AT",
  "properties": {
    "since": 2022
  }
}
```

Unsigned 64-bit IDs should be encoded as decimal JSON strings. JavaScript numbers cannot represent every unsigned 64-bit integer exactly.

##### Query explanation

```http
POST /api/v1/explain
```

Request:

```json
{
  "query": "MATCH (n:Person) WHERE n.name = \"Alice\" RETURN n"
}
```

The response should return the logical or physical query plan without executing the query.

##### Retrieve a node

```http
GET /api/v1/nodes/42
```

##### Retrieve a relationship

```http
GET /api/v1/relationships/93
```

##### Expand a node

```http
GET /api/v1/nodes/42/relationships?direction=outgoing&type=WORKS_AT&limit=100
```

This endpoint supports interactive graph expansion without rerunning the original query.

#### Error responses

API errors should use a stable structure:

```json
{
  "error": {
    "code": "NG_QUERY_PARSE_ERROR",
    "message": "Expected ')' after node pattern",
    "line": 1,
    "column": 18,
    "request_id": "1842"
  }
}
```

HTTP status codes should be used consistently:

* `200` for successful reads and queries.
* `201` for successful creation operations.
* `400` for malformed JSON or invalid parameters.
* `401` for missing authentication.
* `403` for prohibited operations.
* `404` for absent graph objects or endpoints.
* `409` for transaction or constraint conflicts.
* `413` for oversized requests.
* `422` for valid requests containing invalid queries.
* `429` for exceeded request or execution limits.
* `500` for unexpected internal errors.
* `503` when the database is unavailable or busy.

Internal error details, stack memory, database paths, and raw operating-system errors must not be returned to clients.

#### Web query workspace

The browser interface should provide:

* A MiniCypher query editor.
* A Run button.
* Keyboard execution shortcuts.
* Query history.
* Saved favorite queries in browser-local storage.
* Graph, table, raw JSON, and query-plan views.
* Schema and database-statistics panels.
* Expandable node and relationship property inspectors.
* Clear parse and execution diagnostics.
* A visible indication of read-only or read-write mode.
* A visible warning when result limits truncate output.

The initial interface may use a plain `<textarea>` query editor. A larger editor dependency should not be required for the first release.

Example query:

```text
MATCH (a:Person)-[r:WORKS_AT]->(b:Company)
WHERE a.name = "Alice"
RETURN a, r, b
LIMIT 100
```

#### D3.js graph visualization

D3.js should be used for interactive graph rendering.

The graph view should support:

* Force-directed node placement.
* Directed relationship arrows.
* Relationship-type labels.
* Multiple relationships between the same node pair.
* Self-relationships.
* Node labels.
* Node and relationship selection.
* Dragging nodes.
* Zooming and panning.
* Fit-to-screen.
* Reset-layout action.
* Node expansion.
* Property inspection.
* Stable node positions while inspecting results.
* Removal of selected nodes from the current visualization.
* Configurable visualization limits.

The visualization must use stable graph IDs as its internal keys.

Node styling should be deterministic. For example:

* The primary label determines the default node shape or color.
* A stable hash of the label determines its palette position.
* Nodes with the same primary label receive the same style.
* Relationship types receive deterministic styles.
* Styling must not depend on result order.

The interface must not interpret property values as raw HTML.

All user-controlled strings must be inserted using text-safe DOM operations such as `textContent`, not `innerHTML`.

#### Graph interaction

Selecting a node should open an inspector showing:

* Stable node ID.
* Labels.
* Properties.
* Incoming relationship count.
* Outgoing relationship count.

Selecting a relationship should show:

* Stable relationship ID.
* Type.
* Source node ID.
* Target node ID.
* Properties.

The user should be able to expand a selected node by:

* Incoming relationships.
* Outgoing relationships.
* Either direction.
* Selected relationship type.
* Configurable result limit.

Expansion results should merge into the current D3.js visualization without duplicating existing nodes or relationships.

The user should also be able to generate a query from the current selection, such as:

```text
MATCH (n)
WHERE id(n) = 42
RETURN n
```

or:

```text
MATCH (a)-[r:WORKS_AT]->(b)
WHERE id(a) = 42
RETURN a, r, b
LIMIT 100
```

#### Table view

The table view should:

* Display returned columns in query order.
* Preserve typed values.
* Render node and relationship values as expandable summaries.
* Allow copying selected cells.
* Support client-side sorting for already returned rows.
* Clearly indicate null values.
* Clearly indicate truncated results.
* Avoid silently converting 64-bit IDs to JavaScript numbers.

Client-side sorting must not be presented as equivalent to database-level `ORDER BY`.

#### Query history

The browser may store query history in local storage.

History entries may include:

* Query text.
* Execution timestamp.
* Result format.
* Execution duration.
* Success or failure state.

Query results and database property values should not be stored persistently in the browser by default.

The interface should provide a command to clear local query history.

#### Static assets

The web interface should work without an internet connection.

Production builds must serve a vendored D3.js bundle rather than loading it from a public CDN.

Third-party assets must include:

* Version information.
* License notices.
* Source location in release documentation.
* Integrity or checksum information where practical.

A development build may optionally use external assets, but this must never be the default release configuration.

#### Authentication and network exposure

The initial release may support a single static bearer token:

```sh
./nautylus-server \
  --database graph.ng \
  --auth-token-file server.token
```

Clients would send:

```http
Authorization: Bearer <token>
```

Authentication may be omitted only when:

* The server binds exclusively to loopback.
* The user explicitly accepts unauthenticated local access.

The server must print a clear warning when started without authentication.

The first release does not need to provide:

* User accounts.
* Roles.
* Password databases.
* OAuth.
* Session cookies.
* Fine-grained graph authorization.

The documentation must recommend a reverse proxy or another external access-control layer for remote deployments.

Authentication tokens must never be:

* Included in URLs.
* Written to query history.
* Returned in API responses.
* Printed in normal request logs.

#### Browser security

The server should return security headers including:

```text
Content-Security-Policy
X-Content-Type-Options: nosniff
Referrer-Policy: no-referrer
```

The interface should avoid inline scripts where practical so that a restrictive content security policy can be used.

The server must:

* Normalize static-file paths.
* Reject `..` path traversal.
* Reject encoded traversal attempts.
* Serve only files beneath the configured web root.
* Use explicit MIME types.
* Reject unsupported HTTP methods.
* Validate `Content-Type` for request bodies.
* Restrict cross-origin requests by default.
* Disable cross-origin credential access unless explicitly configured.

#### Resource limits

The server should expose configuration for:

```sh
--max-connections 64
--max-header-bytes 16384
--max-body-bytes 1048576
--max-query-bytes 65536
--max-result-rows 10000
--max-result-nodes 5000
--max-result-relationships 10000
--query-timeout-ms 5000
--idle-timeout-ms 30000
```

Limits must be enforced in the server and query executor. Browser-side limits are not sufficient.

When a graph result exceeds its visualization limit, the API may return the permitted prefix while setting:

```json
{
  "truncated": true,
  "truncation_reason": "max_result_nodes"
}
```

The web interface must show this state prominently.

#### Logging

The server should support structured request logging containing:

* Request ID.
* Timestamp.
* HTTP method.
* Endpoint.
* Status code.
* Response size.
* Execution duration.
* Query duration.
* Database generation.

Query text and property values should be excluded from logs by default because they may contain sensitive data.

A debug option may enable query logging explicitly.

#### Server testing

Add tests for:

* HTTP request parsing.
* Partial request reads.
* Partial response writes.
* Invalid methods.
* Invalid paths.
* Oversized headers.
* Oversized bodies.
* Malformed JSON.
* Invalid UTF-8.
* Query parser errors.
* Query execution errors.
* Read-only mutation rejection.
* Transaction conflicts.
* Result truncation.
* Node expansion.
* Static-file path traversal.
* Authentication failures.
* Missing or invalid content types.
* Client disconnects.
* Request timeout behavior.
* Graceful shutdown.
* Reopening the database after server writes.

#### Web interface testing

Add browser-level tests for:

* Running a query.
* Switching between graph, table, and JSON views.
* Rendering nodes and relationships.
* Rendering self-relationships.
* Rendering parallel relationships.
* Selecting graph elements.
* Expanding a node.
* Preserving exact 64-bit IDs.
* Displaying parser diagnostics.
* Displaying truncated-result warnings.
* Escaping malicious labels and property values.
* Clearing query history.
* Operating without external network access.

#### Milestone 8 completion criteria

Milestone 8 is complete when:

1. `nautylus-server` can open an existing database and serve the local web interface.
2. A user can submit a MiniCypher query from the browser.
3. Query execution uses the same parser, planner, and executor as the CLI.
4. Results can be viewed as a table, JSON, or a D3.js graph.
5. Nodes and relationships display readable labels, types, properties, and stable IDs.
6. A user can expand a selected node without duplicating existing graph elements.
7. JavaScript preserves all graph IDs without numeric precision loss.
8. Read-only mode reliably rejects every mutation path.
9. Server-side request, query, traversal, and result limits are enforced.
10. The server rejects malformed and oversized HTTP requests safely.
11. The web interface escapes all graph-controlled text.
12. Static assets work offline and do not require a public CDN.
13. HTTP API and browser tests pass.
14. Normal and sanitizer test suites pass with the server enabled.
15. The core C99 graph library still builds without the server or web interface.





---

## Acceptance criteria

The first useful release must demonstrate that:

1. A small graph can be imported deterministically.
2. Nodes, labels, properties, relationships, and relationship types retain stable IDs after reopening.
3. Duplicate triples are handled according to the selected import policy.
4. Incoming and outgoing traversals return correct results.
5. A MiniCypher query can match a labeled, typed relationship pattern.
6. Exact property lookups use an index when an applicable index exists.
7. A transaction can be committed or rolled back without partial graph mutation.
8. A saved database preserves dictionaries, graph contents, and query results.
9. Corrupt or incompatible database files are rejected safely.
10. Query output includes readable names, typed property values, and stable IDs.
11. Normal and sanitizer test suites pass.
12. The core library compiles as strict C99 without relying on C11 features.

---

## Future optional machine-learning module

Dependency-heavy graph machine learning should remain separate from the core
database. The core library now includes deterministic embeddings, GraphSAGE-style
training/inference, vector search, and link-prediction score APIs.

A future optional module may provide:

* Relation-aware Graph Attention Networks.
* Negative sampling.
* Filtered MRR and Hits@K evaluation.
* Prediction of missing heads or tails.

Such a module may consume immutable graph snapshots through the public database API.

It must not:

* Change the semantics of stored facts.
* Treat predicted links as confirmed relationships automatically.
* Be required to open or query a database.
* Introduce C11 requirements into the C99 storage library.
* Reuse causal tokenization or next-token language-model assumptions.

Predicted links should be returned as scored candidates and added to the graph only through an explicit application-controlled transaction.

---

## Existing code status

The existing GATLM code is a causal token language model and should be treated as reference material only.

The property-graph implementation must not reuse its:

* Byte tokenizer.
* Causal sequence representation.
* Next-token objective.
* Token vocabulary as a graph symbol table.
* Sequence checkpoints as database files.
* Training loop as the graph execution engine.

Reusable low-level components may be retained only after review for:

* C99 compatibility.
* Ownership clarity.
* Bounds safety.
* Deterministic behavior.
* Independence from language-model assumptions.

The graph database should be developed as a distinct root library and executable rather than as another mode inside the language-model program.

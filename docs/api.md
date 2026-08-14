# C API Notes

Include `src/nautylus.h` and link your program with `build/nautylus.o`.

```sh
cc -std=c99 -Wall -Wextra -Wpedantic -O2 -Isrc my_app.c build/nautylus.o -o my_app
```

## Storage Model

* `ng_open()` loads the whole database into memory.
* Mutations are in-memory only until `ng_save()` succeeds.
* `ng_close()` releases memory and does not save automatically.
* `ng_save()` validates the graph before writing.
* If `ng_save()` fails before rename, the old database file should remain intact.
* Rename atomicity and crash durability depend on the operating system and filesystem.
* The current implementation does not coordinate multiple writer processes.
* `ng_graph` is not documented as thread-safe. Use one graph from one thread at a time, or add external synchronization.

## Ownership

Graph ownership:

* `ng_create()` and `ng_open()` return a graph through `ng_graph **out`.
* The caller owns the returned graph.
* Release it with `ng_close()`.
* `ng_close(NULL)` is valid.

Symbol text:

* `ng_symbol()` interns and copies the provided text.
* `ng_symbol_name()` returns a pointer owned by the graph.
* Do not free or mutate returned symbol text.
* Symbol pointers become invalid after `ng_close()` and may become invalid after graph mutation.

Property values:

* `ng_node_set()` and `ng_relationship_set()` copy string and byte values.
* Primitive values are copied by value.
* `ng_node_property()` and `ng_relationship_property()` return graph-owned pointers for strings and bytes.
* Do not free returned property pointers.
* Do not keep returned property pointers after mutating or closing the graph.

Callbacks:

* Relationship, traversal, and match callbacks receive IDs or temporary structs.
* Return non-zero to continue iteration.
* Return zero to stop iteration early.
* Mutating the graph from callbacks is not currently documented as safe.

## IDs and Symbols

* ID `0` is reserved as invalid.
* Node IDs and relationship IDs are stable across save/reopen.
* Deleted IDs are not reused by the current implementation.
* Symbol IDs are assigned by interning text.
* Symbol IDs are stable after save/reopen.
* Symbol interning is permanent for the lifetime of the graph.

## Null and Missing Properties

An absent property and a present `NG_VALUE_NULL` property are different states.

Setting a property to:

```c
ng_value v;
v.type = NG_VALUE_NULL;
v.length = 0;
```

stores a present null value. It does not delete the property.

Delete a property with `ng_node_unset()` or `ng_relationship_unset()`.

## Error Handling Pattern

Check every return value:

```c
#define NG_CHECK(expr) do { \
    status = (expr); \
    if (status != NG_OK) { \
        fprintf(stderr, "%s\n", ng_status_name(status)); \
        goto fail; \
    } \
} while (0)
```

Use one cleanup path:

```c
fail:
    ng_close(g);
    return 1;
```

## Status Values

| Status | Meaning |
| --- | --- |
| `NG_OK` | Success |
| `NG_INVALID_ARGUMENT` | Invalid pointer, ID, path, type, or unsupported text |
| `NG_NOT_FOUND` | Requested object or metadata was absent |
| `NG_PARSE_ERROR` | Malformed TSV or typed value |
| `NG_EXISTS` | Operation would overwrite a reserved backup file |
| `NG_OOM` | Allocation failure |
| `NG_IO_ERROR` | Filesystem failure |
| `NG_CORRUPT` | Invalid native snapshot |
| `NG_LIMIT` | Reserved for implementation limits |

## Import and Export

Triple import:

```c
size_t accepted = 0;
ng_status s = ng_import_triples(g, "triples.tsv", 0, &accepted);
```

CSV triple import:

```c
size_t accepted = 0;
ng_status s = ng_import_triples_csv(g, "triples.csv", 0, &accepted);
```

Property-graph import:

```c
ng_import_diagnostic d;
size_t accepted = 0;
ng_status s = ng_import_property_graph(
    g, "nodes.tsv", "relationships.tsv", 0, &accepted, &d);
```

The `preserve_parallel` argument controls duplicate suppression:

* `0`: suppress duplicate relationships by `(source, type, target)`.
* non-zero: preserve parallel duplicate relationships.

Import functions roll back in-memory changes on failure.

The CLI exposes the same storage paths with workflow-oriented commands:
`nautylus store DB TRIPLES` for triple TSV data, `nautylus store-csv DB TRIPLES_CSV` for triple CSV data, `nautylus store-ng DB NODES RELATIONSHIPS` for property-graph TSV data, `nautylus search DB QUERY` for MiniCypher search, and `nautylus analyze DB` or `nautylus analyse DB` for validation plus graph counts.

## Transactions

Transactions are explicit in-memory snapshots:

```c
ng_transaction *tx = 0;
ng_graph *work = 0;

if (ng_transaction_begin(g, &tx) == NG_OK) {
    work = ng_transaction_graph(tx);
    /* mutate work */
    ng_transaction_commit(tx);
}
```

`ng_transaction_rollback(tx)` discards the working copy. `ng_transaction_commit(tx)` validates the working graph and swaps it into the original graph. IDs allocated inside a rolled-back transaction may remain unused from the caller's point of view.

Transactions are single-process and in-memory. They do not provide multi-process locking and do not write to disk unless the caller later calls `ng_save()`.

## Traversal

`ng_traverse()` performs bounded breadth-first traversal.

```c
ng_traversal_options opt;
opt.direction = NG_DIRECTION_OUTGOING;
opt.types = &type_id;
opt.type_count = 1;
opt.max_depth = 3;
opt.visit_limit = 0;
```

Use `visit_limit` to cap callbacks. A zero limit means no explicit limit.

## Exact Node Scans

`ng_find_nodes()` scans nodes for an exact typed property match, optionally constrained by label.

Supported value types:

* `NG_VALUE_NULL`
* `NG_VALUE_BOOL`
* `NG_VALUE_INT64`
* `NG_VALUE_DOUBLE`
* `NG_VALUE_STRING`
* `NG_VALUE_BYTES`

Double comparisons use exact 64-bit payload equality.

## Snapshot Node Indexes

`ng_node_index_create()` stores persistent metadata declaring an exact-match node index for one `(label, key)` pair. `ng_node_index_drop()` removes that declaration. Use `ng_node_index_count()` and `ng_node_index_get()` to enumerate stored declarations after reopen.

```c
ng_node_index_create(g, person_label, name_key);
ng_save(g);
```

`ng_node_index_build()` builds an explicit in-memory snapshot index for one `(label, key)` pair. The index stores matching node IDs and copied property values at build time.

```c
ng_node_index *index = 0;
ng_node_index_build(g, person_label, name_key, &index);
ng_node_index_find(index, &needle, visit_node, 0);
ng_node_index_free(index);
```

Rebuild in-memory indexes after graph mutations. Native snapshots persist index metadata, not materialized lookup contents.

## Constraint Validation

`ng_require_node_property()` checks that every node matching a label has a non-null property. Pass label `0` to check all nodes. It returns `NG_NOT_FOUND` and writes the first offending node ID when a matching node is missing the property or stores `NG_VALUE_NULL`.

```c
ng_node_id offender = 0;
ng_require_node_property(g, person_label, name_key, &offender);
```

`ng_unique_node_property()` checks that no two matching nodes have equal non-null values for the property. Missing and null values are ignored for uniqueness. It returns `NG_EXISTS` and writes the first duplicate node pair when a duplicate is found.

```c
ng_node_id first = 0, second = 0;
ng_unique_node_property(g, person_label, email_key, &first, &second);
```

These are explicit validation calls. They do not create stored schema metadata by themselves.

`ng_node_constraint_create()` stores a required or unique node-property constraint after first checking the current graph. Stored constraints are persisted in native snapshots and are checked by `ng_node_create_with_properties()`, `ng_node_set()`, `ng_node_unset()`, import completion, `ng_validate()`, `ng_save()`, `ng_open()`, and transaction commit.

```c
ng_node_constraint_create(g,
    NG_NODE_CONSTRAINT_UNIQUE_PROPERTY,
    person_label,
    email_key);
```

Use `ng_node_constraint_count()` and `ng_node_constraint_get()` to enumerate stored constraints. Use `ng_node_constraint_drop()` to remove one.

`ng_node_create_with_properties()` creates a labeled node and copies its initial properties in one call. It preflights required and unique node-property constraints before allocating the node, so constraint failures do not leave a partial node behind.

```c
ng_property props[1];
props[0].key = email_key;
props[0].value.type = NG_VALUE_STRING;
props[0].value.length = strlen(email);
props[0].value.as.string = email;

ng_node_create_with_properties(g, &person_label, 1, props, 1, &node);
```

`ng_node_create()` remains available as a create-then-set API. Required-property constraints are therefore checked when properties are unset/nulled and at validation boundaries, not at initial node allocation.

## MiniCypher Node Queries

`ng_query_nodes()` executes the current node-returning MiniCypher subset:

```c
ng_query_nodes(g, "MATCH (n:Person) WHERE n.name = \"Alice\" RETURN n",
    visit_node, 0);
```

Supported forms:

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
```

Relationship patterns are directed by default, with `->`, `<-`, and undirected reads supported in the generic pipeline. Bounded hop counts from 1 to 64 are supported with `*N` or `*N..M`. `ng_query_nodes()` returns matching node IDs for single node-ID returns. `ng_query_print()` also supports `RETURN n.id`, `RETURN m.id`, node-property projections such as `RETURN n.name`, and tab-separated multi-column projection rows. The broader execution API supports relationship variables/properties, `WITH`, `UNWIND`, `OPTIONAL MATCH`, parameters, writes, aggregation, `ORDER BY`, `SKIP`, and `LIMIT`.

The write-capable API is exposed through `ng_query_execute()` and `ng_query_execute_params()`. Writes are executed transactionally: if parsing, execution, property validation, output, or commit fails, the graph is rolled back. The current write subset includes comma-separated `CREATE` and `MERGE` patterns, scalar-property and map-based `SET`, `REMOVE` property/label targets, and comma-separated node/relationship `DELETE` and `DETACH DELETE` targets. CREATE and MERGE property-map values may be row-dependent scalar expressions, such as `MERGE (n:Value {value: x + 1})` after `UNWIND ... AS x`; MERGE evaluates the same values for lookup and creation. `SET n += {key: value}` merges entries, while `SET n = {key: value}` replaces the property set. Null map values remove properties. Node deletion removes incident relationships before removing the node.

Named parameters use `ng_parameter` values and are bound through `ng_query_execute_params()` or `ng_query_print_params()` without textual substitution:

```c
ng_parameter parameter = { "name", { NG_VALUE_STRING, 3, { .string = "Joe" } } };
int mutated = 0;
ng_query_execute_params(g,
    "MATCH (a:Person) WHERE a.name = $name RETURN a",
    &parameter, 1, stdout, &mutated);
```

Supported parameter values are the existing `ng_value` types, including null and `NG_VALUE_LIST`. Missing parameters return `NG_NOT_FOUND`; extra parameters are ignored. `UNWIND $items AS item` expands list-valued parameters without textual query substitution.

The supported procedure-style query is a seeded random walk:

```text
MATCH (a) CALL randomWalk(a, 5, 42) YIELD node RETURN node
```

It includes the start node and emits one row per visited node. The Cypher form uses outgoing relationships and all relationship types. The typed C API exposes direction and relationship-type filtering:

```c
ng_random_walk_options options = {
    NG_DIRECTION_EITHER, relationship_type, 100, 42
};
ng_node_id path[101];
size_t path_count = 0;
ng_random_walk(g, start, &options, path, 101, &path_count);
```

Applications can register additional row procedures with `ng_procedure_register()`. A handler receives `ng_procedure_argument` values. Scalar arguments contain the normal `ng_value`; direct node and relationship variables are passed as typed `NG_PROCEDURE_NODE` or `NG_PROCEDURE_RELATIONSHIP` arguments with their graph IDs. Handlers fill named `ng_procedure_field` results. Query syntax is `CALL name(expr, ...) YIELD field [AS alias], ...`; aliases become the row variables and can be consumed by later `WITH`, `MATCH`, and `RETURN` clauses. Registrations belong to the graph handle and are copied into transactional working graphs.

`UNION` and `UNION ALL` combine query branches with compatible column metadata. Column names come from aliases or the projection expression and must agree; numeric integer/double types are compatible, while other known type mismatches fail. Null-only and empty branches retain their statically known schema without requiring emitted rows. Plain `UNION` removes duplicate rendered rows, while `UNION ALL` preserves them. `UNION DISTINCT` is accepted as an explicit spelling of plain `UNION`. Branches execute inside the same write transaction when any branch mutates the graph, so a later branch failure rolls back earlier branch writes.

Supported predicate literals:

* quoted strings without escape sequences;
* signed 64-bit integers;
* `true`;
* `false`;
* `null`.

`ng_query_explain()` parses the same subset and writes a short textual plan into a caller-provided buffer.

Unsupported syntax returns `NG_PARSE_ERROR`. Nested map values are supported through `NG_VALUE_MAP`; map literals can be evaluated in `WITH`, `RETURN`, `SET`, `CREATE`, and `MERGE`. Path values, subqueries, and full Cypher compatibility are not implemented.

## Analytics

The dependency-free analytics API currently includes degree centrality, PageRank, weakly and strongly connected components, triangle count, local clustering coefficient, common neighbors, preferential attachment, total neighbors, topological sort, and seeded random walks. Analytics operate on the in-memory graph and write into caller-owned buffers. A small output buffer returns `NG_LIMIT` and reports the required count where the API provides an output-count pointer. Heavy weighted-path, community, similarity, embedding, and flow algorithms remain future work.

# Snapshot Format

This document describes the current native `nautylus` snapshot format. It is implementation documentation for the alpha format, not a long-term compatibility guarantee.

## Compatibility Policy

Current format:

* magic: `NAUTY`
* version byte: `3`
* integer encoding: unsigned little-endian 64-bit fields
* checksum: 32-bit FNV-1a over the payload

The loader accepts version 1 snapshots as constraint-free, index-free databases and version 2 snapshots as index-free databases with constraints. It writes version 3 snapshots. It rejects other unsupported versions. There is not yet a migration tool for future-incompatible snapshots.

## File Layout

```text
header: 32 bytes
payload: header.payload_length bytes
```

Header layout:

| Offset | Size | Field |
| --- | ---: | --- |
| 0 | 5 | Magic bytes `NAUTY` |
| 5 | 1 | Version byte, currently `2` |
| 6 | 2 | Reserved, currently ignored |
| 8 | 8 | Payload length |
| 16 | 8 | Header generation/check field, currently `next_node ^ next_rel ^ next_sym` |
| 24 | 8 | Payload checksum stored in the low 32 bits |

The current checksum is FNV-1a 32-bit. It detects accidental corruption but is not authentication.

## Payload Layout

All integers are little-endian unsigned 64-bit values unless noted otherwise.

Top-level fields:

```text
symbol_count
node_count
relationship_count
constraint_count
index_count
next_symbol_id
next_node_id
next_relationship_id
symbols...
nodes...
relationships...
constraints...
indexes...
```

Version 1 snapshots omit `constraint_count`, `index_count`, `constraints...`, and `indexes...`. Version 2 snapshots omit `index_count` and `indexes...`.

Symbol record:

```text
symbol_id
text_length
text_bytes
```

Node record:

```text
node_id
label_count
label_symbol_id...
property_count
properties...
```

Relationship record:

```text
relationship_id
source_node_id
target_node_id
type_symbol_id
property_count
properties...
```

Constraint record:

```text
constraint_kind
label_symbol_id
key_symbol_id
```

Constraint kinds:

| Kind | Meaning |
| ---: | --- |
| 1 | Required node property |
| 2 | Unique node property |

`label_symbol_id` may be `0` to target all nodes. `key_symbol_id` must reference an existing symbol.

Index metadata record:

```text
label_symbol_id
key_symbol_id
```

Index metadata records declare exact-match node indexes for `(label, key)` pairs. The snapshot persists declarations only, not materialized lookup contents. `label_symbol_id` may be `0` to target all nodes. `key_symbol_id` must reference an existing symbol.

Property record:

```text
key_symbol_id
value
```

Value record:

```text
value_type
length
payload
```

Value payloads:

| Type | Payload |
| --- | --- |
| `NG_VALUE_NULL` | none |
| `NG_VALUE_BOOL` | one 64-bit integer, `0` or `1` |
| `NG_VALUE_INT64` | one 64-bit two's-complement payload |
| `NG_VALUE_DOUBLE` | one exact 64-bit IEEE-754 payload |
| `NG_VALUE_STRING` | `length` bytes followed by an in-memory NUL terminator after load |
| `NG_VALUE_BYTES` | `length` raw bytes |

## Load Validation

`ng_open()` rejects:

* missing or invalid magic bytes;
* unsupported versions;
* payload lengths that do not fit in memory;
* truncated payloads;
* trailing bytes after the declared payload;
* checksum mismatches;
* unconsumed bytes inside the payload;
* invalid IDs;
* duplicate symbols, nodes, relationships, labels, or properties;
* relationships that reference absent nodes;
* invalid symbol references;
* invalid constraint kinds or duplicate constraints;
* duplicate or invalid index metadata;
* stored constraints that are violated by graph contents;
* invalid value types;
* invalid bool payloads;
* missing string or byte pointers for non-empty values.

## Save Semantics

`ng_save()`:

1. Validates the in-memory graph.
2. Encodes the payload in memory.
3. Writes `FILE.tmp`.
4. Closes `FILE.tmp`.
5. Renames `FILE.tmp` over `FILE`.

If validation, encoding, writing, or closing fails before rename, the old snapshot remains in place. Rename replacement is expected to be atomic on POSIX filesystems, but crash durability depends on filesystem behavior and directory sync semantics. The current implementation does not fsync the file or containing directory.

## Determinism

The snapshot preserves IDs and exact typed value bits. It does not currently claim byte-identical snapshots for independently constructed equivalent graphs, because symbol and record ID assignment follows mutation order.

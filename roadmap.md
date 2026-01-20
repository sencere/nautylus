# Nautylus Roadmap
**Embedded Graph + Vector Meaning Engine for Recommendation and Ideation**

Nautylus is a **single-node, embedded** database that fuses **graph structure + vector similarity + deterministic scoring + explainability** so teams can build **recommendations** and **ideation/synthesis** features without glue-code pain.

It is **not** a model-training system and **does not** embed an agent. Agents and apps query it.

---

## 0. Positioning

Nautylus focuses on workloads where the hard problems are not model training, but **retrieval, scoring, and explainability**:

- candidate generation is ad-hoc
- scoring logic is scattered across services
- explanations are bolted on
- pipelines become non-deterministic and untestable
- integration adds infra overhead

**Nautylus' bet:** these workloads fit on one machine and benefit from an embedded engine that makes:

- graph + vector retrieval trivial
- scoring composable and deterministic
- explanations first-class and testable
- integration friction near zero

---

## 1. Core Principles

1. **Embedded-first** — in-process library, no server required.
2. **Single-node excellence** — locality and determinism before distribution.
3. **C core, stable ABI** — auditable, portable, bindings-friendly.
4. **Determinism by default** — stable ordering, reproducible scores.
5. **Meaning primitives** — retrieval + scoring + explainability at the center.
6. **Minimal surface area** — fewer features, stronger guarantees.

---

## 2. What Nautylus Enables

### Recommendation (Mode A)
- personalized ranking
- similar-items
- session-based suggestions
- graph-aware re-ranking
- hybrid (graph + vector) retrieval

### Ideation / Synthesis (Mode B)
- "novel but relevant" retrieval
- diversity and coverage
- constraint-driven exploration ("must include X, avoid Y")
- explainable "why this combination is interesting"
- anti-duplication / distance-from-corpus novelty signals

Same engine, same primitives; only the objective changes.

---

## 3. System Architecture (Conceptual)

Bindings (Python v1, then others)
│
▼
Stable C ABI
│
▼
Execution Engine (Pipelines)
- FIND / FILTER / EXPAND / JOIN
- RANK (signals + weights)
- DIVERSIFY / NOVELTY
- EXPLAIN (evidence + breakdown)
│
▼
Graph + Vector Core
- adjacency + properties
- vector fields + index
│
▼
Storage
- WAL
- Snapshots
- Versioning + checksums

---

## 4. Roadmap Overview

| Phase | Focus | Outcome |
|------|-------|---------|
| I | Core graph + memory model | Fast deterministic in-memory graph |
| II | Vector core + hybrid retrieval | Graph<->Vector fusion with stable ranking |
| III | Scoring pipelines | Composable signals + deterministic ranking |
| IV | Explainability | Evidence + score decomposition |
| V | Persistence | WAL + snapshots + reproducible restarts |
| VI | Integration surface | C ABI + first bindings + examples |
| VII | Data connectivity | Attach/read-through + explicit materialization |
| VIII | Hardening | fuzzing, benchmarks, stability |
| IX | Bindings expansion | multiple languages, zero-pain embedding |

---

## Phase I — Graph Core (Single-node)

### Milestone 1 — Memory Model
- arena/slab allocator for nodes/edges/properties
- stable internal IDs (`uint64_t`) mapped to dense storage
- deterministic iteration order documented

**Acceptance**
- 1M nodes with <10% overhead
- Valgrind + ASAN clean teardown
- iteration order stable across runs

### Milestone 2 — Graph Operations
- CRUD nodes/edges
- edge types + weights + timestamps
- referential integrity (no dangling edges)
- neighbor iterators

**Acceptance**
- randomized mutation tests maintain invariants
- baseline traversals match reference outputs

---

## Phase II — Vector Core + Hybrid Retrieval

### Milestone 3 — Vector Fields
- node-attached vectors (float32)
- fixed dimension per index
- exact kNN baseline

**Acceptance**
- exact kNN correctness verified
- deterministic top-k ordering (tie-break rules)

### Milestone 4 — Hybrid Retrieval Patterns
Patterns:
- vector -> graph expansion
- graph -> vector search
- intersection + re-rank

**Acceptance**
- hybrid results deterministic
- graph-only and vector-only unchanged
- weight configs tested

---

## Phase III — Scoring Pipelines (Meaning Engine)

### Milestone 5 — Pipeline VM (Minimal)
- operator pipeline: `scan -> filter -> expand -> project -> rank`
- builder-style API (no DSL commitment yet)
- stable `EXPLAIN` operator tree

**Acceptance**
- multi-hop traversals correct
- `EXPLAIN` stable across runs

### Milestone 6 — RANK (Signals + Weights)
Signals:
- graph proximity (e.g., PPR-lite, co-occurrence, neighborhood overlap)
- vector similarity
- recency/popularity boosts
- constraints as hard filters

**Acceptance**
- reproducible scores across runs
- component scores sum to final (within tolerance)

---

## Phase IV — Explainability (First-class)

### Milestone 7 — EXPLAIN Payload
For each result:
- score breakdown per signal
- evidence neighbors / paths
- similarity justification (vector contribution)
- applied boosts / filters

**Acceptance**
- explanation equals computed score (within tolerance)
- evidence paths reproducible across runs

---

## Phase V — Persistence

### Milestone 8 — WAL + Snapshots
- append-only WAL
- periodic snapshots
- versioned headers + checksums
- recovery: snapshot -> WAL replay

**Acceptance**
- kill -9 recovery passes
- corrupt segments detected
- identical query outputs pre/post restart

---

## Phase VI — Integration Surface (Zero-pain Embedding)

### Milestone 9 — Stable C API (v1)
- opaque handles
- explicit lifetimes
- single-writer / multi-reader transactions
- stable ABI policy

**Acceptance**
- sanitizer clean
- minimal C examples: load, write, rank, explain

### Milestone 10 — First Binding (Python v1)
- thin wrapper over C API
- wheels via CI
- demo notebooks:
  - recommendation
  - ideation (novel + diverse)

**Acceptance**
- Python results identical to C
- demos under 50 LOC each

---

## Phase VII — Data Connectivity (No Pain, No Magic)

### Milestone 11 — Explicit Materialization
- CLI to materialize only what's needed:
  - entity IDs
  - selected properties
  - edges
  - embeddings
- incremental updates (by watermark)

**Acceptance**
- repeatable builds (same input snapshot -> same index)
- safe failure modes (partial builds rejected)

### Milestone 12 — Attach / Read-through (Optional)
- attach Parquet/CSV first
- optional SQLite/Postgres read-only later

**Acceptance**
- attach never mutates source
- deterministic outputs given same source snapshot

---

## Phase VIII — Hardening

### Milestone 13 — Fuzzing + Property Testing
- WAL fuzzing
- pipeline fuzzing
- mutation tests for graph invariants

### Milestone 14 — Benchmarks
- microbenchmarks in CI
- workload suites:
  - rec: similar-items, personalized ranking
  - ideation: novelty + diversity constrained retrieval

---

## Phase IX — Bindings Expansion (Integration Everywhere)

### Milestone 15 — Additional Bindings (Prioritized)
- Node.js
- Rust
- Go
- Java/Kotlin
- PHP (extension or FFI wrapper)

**Acceptance**
- consistent API surface across languages
- golden tests: identical outputs across bindings

---

## v1 Cutline (Explicit Non-goals)
- no clustering/sharding
- no built-in agent or model training
- no "import everything" auto-ETL
- no giant SQL dialect in v1
- no non-deterministic defaults

---

## Definition of "No Pain to Integrate"
A user can:
1. embed Nautylus (library) or run a local binary (optional)
2. materialize/attach data with one command
3. call `find/rank/explain/diversify/novelty` from their language
4. get back IDs + scores + explanations

No glue microservices required.

---

## Next (Optional Spec Docs)
- v1 API (C headers + semantic contract)
- scoring signal library (minimal set + guarantees)
- determinism rules (ordering, float ties, stable hashing)
- ideation cookbook (novelty/diversity patterns)

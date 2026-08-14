# Tested Limits and Baseline

This document records the small, repeatable limits covered by the current alpha test suite. It is not a capacity guarantee.

## Tested Functional Limits

The normal `make test` target covers:

* native snapshots with save/reopen validation;
* property-graph import/export round trips;
* strict rejection of native snapshots with trailing bytes;
* MiniCypher bounded relationship expansion up to accepted depths;
* MiniCypher rejection of relationship depths greater than 64;
* persisted node constraints and index metadata;
* allocation-failure rollback during property-graph import;
* deterministic repeated property-graph exports.

The `make examples` target compiles the runnable examples in `examples/*.c`.
During local verification the examples are also run manually to exercise direct
graph creation, MiniCypher execution, analytics, GraphSAGE training, and
vector-index search/persistence paths.

The CLI regression suite currently runs `nautylus bench bench.ng 128`, which creates a 128-node chain, saves it, reopens it, validates it, builds an exact-match node index, and checks one indexed lookup.

## Benchmark Command

Run:

```sh
make perf
```

or:

```sh
./build/nautylus bench build/perf.ng 1000
```

The `bench` command accepts node counts from 1 to 100000. It prints graph counts, indexed lookup matches, and elapsed CPU seconds from C99 `clock()`.

Example output shape:

```text
nodes: 1000
relationships: 999
symbols: 3
index-matches: 1
seconds: 0.010000
```

Timing depends on hardware, compiler, filesystem, and current system load. Treat it as a local smoke baseline for regressions, not as a published throughput claim.

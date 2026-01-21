## Graph Core (Milestone 1)

This repo now includes a fixed-capacity slab allocator that backs nodes,
edges, and properties. Each allocation yields a stable 64-bit ID:

- upper 32 bits: generation
- lower 32 bits: 1-based slot index

IDs stay stable across reuse because generations increment on every
allocation. An ID is invalid if the generation does not match.

### Deterministic Iteration

Nodes, edges, and properties iterate in creation order. Deletions preserve
the relative order of remaining items. Given the same sequence of mutations,
iteration is stable across runs.

## Graph Operations (Milestone 2)

Edges are created only when both endpoints exist, and deleting a node
removes its incident edges to prevent dangling references.

Neighbor iterators walk outgoing or incoming edges in creation order for
each node.

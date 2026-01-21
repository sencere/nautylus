## Vector Core (Milestone 3)

The vector index is a fixed-capacity, exact kNN baseline over float32
embeddings. Each index has a fixed dimension; vectors are attached by
node ID. The kNN API returns results sorted by L2 squared distance, then
ID as a deterministic tie-break.

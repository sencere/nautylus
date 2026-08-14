# GraphSAGE-Style Embeddings

Nautylus provides `ng_graphsage()` for deterministic, dependency-free graph embeddings. It is useful when each node already has numeric features and the embedding should also reflect its graph neighborhood.

The implementation performs mean neighborhood aggregation followed by a seeded nonlinear projection for each layer. The reusable model API supports configurable layer weights, neighborhood sampling, feature normalization, binary save/load, cosine vector search, supervised training, and reusable vector indexes.

For a complete runnable example covering training diagnostics, prediction helpers,
HNSW-style vector search, and persistence, build `make examples` and run
`./build/graphsage_vector`.

## API

```c
ng_status ng_graphsage(const ng_graph* g,
                       ng_direction direction,
                       ng_symbol_id type,
                       uint32_t iterations,
                       size_t input_dimensions,
                       size_t output_dimensions,
                       const double* features,
                       uint64_t seed,
                       double* out,
                       size_t capacity,
                       size_t* out_count);
```

Arguments:

* `g`: open graph to analyze.
* `direction`: `NG_DIRECTION_OUTGOING`, `NG_DIRECTION_INCOMING`, or `NG_DIRECTION_EITHER`.
* `type`: relationship type filter; pass `0` for all relationship types.
* `iterations`: number of neighborhood aggregation layers.
* `input_dimensions`: number of values per input node.
* `output_dimensions`: number of values per output node.
* `features`: caller-owned row-major input matrix with `node_count * input_dimensions` values.
* `seed`: deterministic projection seed. The same graph, features, and seed produce the same result.
* `out`: caller-owned row-major output matrix with `node_count * output_dimensions` values.
* `capacity`: number of `double` slots available in `out`, not the number of nodes.
* `out_count`: receives the number of nodes represented in the output.

Rows use the graph's node enumeration order. Obtain node IDs in the same order with the node enumeration API and keep that mapping alongside the matrix. The output row for node `i` starts at `out[i * output_dimensions]`.

## Example

```c
/* Five nodes, two input features per node. */
const double features[5 * 2] = {
    1.0, 0.0,
    0.0, 1.0,
    1.0, 1.0,
    0.5, 0.5,
    0.2, 0.8
};
double embeddings[5 * 3];
size_t node_count = 0;

ng_status status = ng_graphsage(
    g,
    NG_DIRECTION_EITHER,
    0,
    2,                  /* aggregation layers */
    2,                  /* input dimensions */
    3,                  /* output dimensions */
    features,
    42,                 /* deterministic seed */
    embeddings,
    5 * 3,
    &node_count);

if (status != NG_OK) {
    /* Handle ng_status_name(status) or the application error policy. */
}
```

With this call, `embeddings` contains five three-dimensional vectors. Nodes with similar features and neighborhoods tend to receive similar representations, which can be used as input to a similarity search, clustering step, recommender, or link-prediction model.

## Reusable model and search

```c
ng_graphsage_config config = {2, 4, 8, 10, 1, 42};
ng_graphsage_model* model = NULL;
ng_graphsage_model_create(&config, &model);
ng_graphsage_model_infer(model, g, NG_DIRECTION_EITHER, 0,
                         features, embeddings, node_count * 8, &node_count);
ng_graphsage_model_save(model, "templates.graphsage");
ng_graphsage_model_free(model);
```

Restore with `ng_graphsage_model_load()`. Search row-major vectors directly:

```c
ng_vector_score hits[10];
size_t hit_count = 0;
ng_vector_search_cosine(embeddings, node_count, 8, query, 10,
                        hits, 10, &hit_count);
/* hits[i].index is the embedding row; hits[i].score is cosine similarity. */
```

Results are sorted from highest to lowest similarity. The search is an in-memory scan. For repeated queries, build a vector index:

```c
ng_vector_hnsw_config ann_config = {16, 64, 32};
ng_vector_index* index = NULL;

ng_vector_index_create_hnsw(embeddings, node_count, 8, &ann_config, &index);
ng_vector_index_search_hnsw_cosine(index, query, 10, 32, hits, 10, &hit_count);
ng_vector_index_save(index, "embeddings.ngv");
ng_vector_index_free(index);
```

## Supervised training

`ng_graphsage_model_train()` accepts one target row per graph node and trains with mean-squared error. For mini-batches, validation, binary classification targets, or multiclass classification targets, use `ng_graphsage_model_train_ex()` with `ng_graphsage_training_options`. Set `loss` to `NG_GRAPHSAGE_LOSS_MSE`, `NG_GRAPHSAGE_LOSS_BINARY_CROSS_ENTROPY`, or `NG_GRAPHSAGE_LOSS_SOFTMAX_CROSS_ENTROPY`. Binary-cross-entropy targets must be in the `[0, 1]` range. Softmax-cross-entropy targets are row-major class probability rows and each target row must sum to `1.0`. The report contains separate training and validation losses plus classification accuracy, precision, and recall where the selected loss is classification-oriented.

Training uses the same cached sampled-neighborhood forward path as inference, then applies analytic backpropagation through mean aggregation, tanh activations, weights, and biases. The implementation supports multi-layer models, optional feature normalization, deterministic neighborhood sampling, mini-batches, validation splits, MSE, binary cross-entropy, and softmax cross-entropy. Training prepares deterministic sampled-neighbor lists once per call, builds a compact batch-relevant computation graph from those sampled neighborhoods, and reuses gradient buffers across batch updates. Report generation evaluates loss and classification metrics from a shared forward pass per split to avoid redundant inference work. Finite differences are retained only in test-only gradient checks that compare analytic parameter gradients against a numerical reference.

Use `ng_graphsage_model_train_ex_diagnostics()` when the caller needs training metadata beyond the final report. Pass a zero-initialized `ng_graphsage_training_diagnostics` and optional caller-owned buffers for `epoch_training_losses`, `epoch_validation_losses`, and `validation_rows`. `epoch_capacity` limits how many per-epoch losses are written; `epoch_count` still reports the total number of completed epochs. `validation_start`, `validation_row_count`, `validation_rows`, and `validation_seed` describe the deterministic validation split. `batch_count`, `cached_forward_reuses`, `sampled_neighbor_count`, `subgraph_node_references`, `subgraph_edge_references`, and `gradient_buffer_bytes` provide lightweight runtime/memory diagnostics for the optimized large-batch path. Set `convergence_tolerance` to a positive value to stop once the absolute change in training loss between consecutive epochs is at or below that threshold; `converged`, `epochs_run`, and `convergence_delta` report the result. The existing `ng_graphsage_model_train()` and `ng_graphsage_model_train_ex()` defaults are unchanged.

Use `ng_graphsage_model_predict_probabilities()` to convert model outputs into row-major softmax probabilities, or `ng_graphsage_model_predict_classes()` to receive the highest-probability class index and optional confidence per node.

For reusable vector search over embeddings, create an `ng_vector_index` with `ng_vector_index_create()` or `ng_vector_index_create_hnsw()`. Query it exactly with `ng_vector_index_search_cosine()`, query it approximately with `ng_vector_index_search_approx_cosine()`, query the older flat ANN graph with `ng_vector_index_search_ann_cosine()`, or query the layered HNSW-style graph with `ng_vector_index_search_hnsw_cosine()`. Persist indexes with `ng_vector_index_save()` / `ng_vector_index_load()`.

Approximate search uses deterministic random-projection signatures to preselect a tunable number of candidates, then reranks those candidates by exact cosine score. The HNSW-style path assigns deterministic levels, inserts vectors in index order, stores bounded neighbor lists per layer, and uses greedy upper-layer descent followed by a bounded base-layer search. `ng_vector_hnsw_config.m` controls the maximum neighbors per layer, `ef_construction` controls insertion candidate breadth, and `ef_search` is the default search breadth when a search call passes zero. Tests benchmark recall against exact search for full, reduced, flat-ANN, and HNSW candidate paths.

## Validation and limitations

The function returns `NG_INVALID_ARGUMENT` for missing features, zero dimensions, zero iterations, invalid direction, or an invalid relationship type. It returns `NG_LIMIT` when the output capacity is too small or a matrix size overflows, and `NG_OOM` when temporary buffers cannot be allocated.

The current implementation is intended for small in-memory graphs. Mini-batches, validation splits, model serialization, feature normalization, supervised MSE training, binary cross-entropy training, softmax cross-entropy training, prediction helpers, sampled subgraph training, optimized split reporting, persisted exact/approximate/ANN/HNSW vector search, and deterministic HNSW-style multi-layer ANN indexes are supported. Edge features, deletions from vector indexes, and distributed training are not yet implemented.

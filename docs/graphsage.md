# GraphSAGE-Style Embeddings

Nautylus provides `ng_graphsage()` for deterministic, dependency-free graph embeddings. It is useful when each node already has numeric features and the embedding should also reflect its graph neighborhood.

The implementation performs mean neighborhood aggregation followed by a seeded nonlinear projection for each layer. The reusable model API supports configurable layer weights, neighborhood sampling, feature normalization, binary save/load, cosine vector search, and supervised MSE training.

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

Results are sorted from highest to lowest similarity. The search is an in-memory scan, so a dedicated ANN index is still needed for very large collections.

## Supervised training

`ng_graphsage_model_train()` accepts one target row per graph node and trains with mean-squared error. For mini-batches, validation, or binary classification targets, use `ng_graphsage_model_train_ex()` with `ng_graphsage_training_options`. Set `loss` to `NG_GRAPHSAGE_LOSS_MSE` or `NG_GRAPHSAGE_LOSS_BINARY_CROSS_ENTROPY`; binary-cross-entropy targets must be in the `[0, 1]` range. The report contains separate training and validation losses.

Training uses the same cached sampled-neighborhood forward path as inference, then applies analytic backpropagation through mean aggregation, tanh activations, weights, and biases. The implementation supports multi-layer models, optional feature normalization, deterministic neighborhood sampling, mini-batches, validation splits, MSE, and binary cross-entropy. Finite differences are retained only in test-only gradient checks that compare analytic parameter gradients against a numerical reference.

Use `ng_graphsage_model_train_ex_diagnostics()` when the caller needs training metadata beyond the final report. Pass a zero-initialized `ng_graphsage_training_diagnostics` and optional caller-owned buffers for `epoch_training_losses`, `epoch_validation_losses`, and `validation_rows`. `epoch_capacity` limits how many per-epoch losses are written; `epoch_count` still reports the total number of completed epochs. `validation_start`, `validation_row_count`, `validation_rows`, and `validation_seed` describe the deterministic validation split. Set `convergence_tolerance` to a positive value to stop once the absolute change in training loss between consecutive epochs is at or below that threshold; `converged`, `epochs_run`, and `convergence_delta` report the result. The existing `ng_graphsage_model_train()` and `ng_graphsage_model_train_ex()` defaults are unchanged.

## Validation and limitations

The function returns `NG_INVALID_ARGUMENT` for missing features, zero dimensions, zero iterations, invalid direction, or an invalid relationship type. It returns `NG_LIMIT` when the output capacity is too small or a matrix size overflows, and `NG_OOM` when temporary buffers cannot be allocated.

The current implementation is intended for small in-memory graphs. Mini-batches, validation splits, model serialization, feature normalization, supervised MSE training, and binary cross-entropy training are supported. Classification-specific multiclass losses, edge features, optimized large-batch training, and a persistent vector index are not yet implemented.

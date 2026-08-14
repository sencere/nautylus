#include "nautylus.h"

#include <stdio.h>
#include <string.h>

#define NG_CHECK(expr)                                                                             \
    do {                                                                                           \
        status = (expr);                                                                           \
        if (status != NG_OK) {                                                                     \
            fprintf(stderr, "%s: %s\n", #expr, ng_status_name(status));                           \
            goto fail;                                                                             \
        }                                                                                          \
    } while (0)

int main(void) {
    ng_graph* g = NULL;
    ng_graphsage_model* model = NULL;
    ng_graphsage_model* loaded_model = NULL;
    ng_vector_index* index = NULL;
    ng_vector_index* loaded_index = NULL;
    ng_status status = NG_OK;
    ng_symbol_id person = 0, knows = 0;
    ng_node_id nodes[5];
    ng_relationship_id relationship = 0;
    ng_graphsage_config config = {2, 2, 3, 2, 1, 42};
    ng_graphsage_training_options options = {5, 0.02, 2, 0.2, 7, NG_GRAPHSAGE_LOSS_MSE};
    ng_graphsage_training_report report;
    ng_graphsage_training_diagnostics diagnostics;
    ng_vector_hnsw_config hnsw = {4, 8, 8};
    ng_vector_score hits[3];
    double embeddings[15];
    double probabilities[15];
    double confidence[5];
    double train_history[8];
    double validation_history[8];
    size_t classes[5];
    size_t count = 0, i;
    const double features[10] = {
        1.0, 0.0,
        0.9, 0.1,
        0.0, 1.0,
        0.1, 0.9,
        0.5, 0.5
    };
    const double targets[15] = {
        1.0, 0.0, 0.0,
        1.0, 0.0, 0.0,
        0.0, 1.0, 0.0,
        0.0, 1.0, 0.0,
        0.0, 0.0, 1.0
    };

    NG_CHECK(ng_create(&g, "graphsage-example.ng"));
    NG_CHECK(ng_symbol(g, "Person", &person));
    NG_CHECK(ng_symbol(g, "KNOWS", &knows));
    for (i = 0; i < 5; i++)
        NG_CHECK(ng_node_create(g, &person, 1, &nodes[i]));
    NG_CHECK(ng_relationship_create(g, nodes[0], knows, nodes[1], &relationship));
    NG_CHECK(ng_relationship_create(g, nodes[1], knows, nodes[2], &relationship));
    NG_CHECK(ng_relationship_create(g, nodes[2], knows, nodes[3], &relationship));
    NG_CHECK(ng_relationship_create(g, nodes[3], knows, nodes[4], &relationship));
    NG_CHECK(ng_relationship_create(g, nodes[4], knows, nodes[0], &relationship));

    NG_CHECK(ng_graphsage_model_create(&config, &model));

    memset(&report, 0, sizeof(report));
    memset(&diagnostics, 0, sizeof(diagnostics));
    for (i = 0; i < 8; i++) {
        train_history[i] = -1.0;
        validation_history[i] = -1.0;
    }
    diagnostics.epoch_training_losses = train_history;
    diagnostics.epoch_validation_losses = validation_history;
    diagnostics.epoch_capacity = 8;
    diagnostics.validation_rows = NULL;
    diagnostics.validation_row_capacity = 0;
    diagnostics.convergence_tolerance = 0.0;

    NG_CHECK(ng_graphsage_model_train_ex_diagnostics(
        model, g, NG_DIRECTION_EITHER, knows, features, targets, &options, &report, &diagnostics));
    printf("training loss: %.6f, validation loss: %.6f, epochs: %zu\n",
           report.training_loss, report.validation_loss, diagnostics.epochs_run);

    NG_CHECK(ng_graphsage_model_infer(model, g, NG_DIRECTION_EITHER, knows, features, embeddings,
                                      15, &count));
    printf("embedding rows: %zu\n", count);

    NG_CHECK(ng_graphsage_model_predict_probabilities(model, g, NG_DIRECTION_EITHER, knows,
                                                      features, probabilities, 15, &count));
    NG_CHECK(ng_graphsage_model_predict_classes(model, g, NG_DIRECTION_EITHER, knows, features,
                                                classes, confidence, 5, &count));
    printf("first class: %zu confidence %.6f\n", classes[0], confidence[0]);

    NG_CHECK(ng_graphsage_model_save(model, "graphsage-example.model"));
    NG_CHECK(ng_graphsage_model_load("graphsage-example.model", &loaded_model));

    NG_CHECK(ng_vector_search_cosine(embeddings, 5, 3, embeddings, 3, hits, 3, &count));
    printf("exact nearest: %zu %.6f\n", hits[0].index, hits[0].score);
    NG_CHECK(ng_vector_index_create_hnsw(embeddings, 5, 3, &hnsw, &index));
    NG_CHECK(ng_vector_index_search_approx_cosine(index, embeddings, 3, 4, hits, 3, &count));
    NG_CHECK(ng_vector_index_search_ann_cosine(index, embeddings, 3, 2, 5, hits, 3, &count));
    NG_CHECK(ng_vector_index_search_hnsw_cosine(index, embeddings, 3, 8, hits, 3, &count));
    printf("hnsw nearest: %zu %.6f\n", hits[0].index, hits[0].score);
    NG_CHECK(ng_vector_index_save(index, "graphsage-vectors.ngv"));
    NG_CHECK(ng_vector_index_load("graphsage-vectors.ngv", &loaded_index));
    NG_CHECK(ng_vector_index_search_hnsw_cosine(loaded_index, embeddings, 3, 8, hits, 3, &count));

    ng_vector_index_free(loaded_index);
    ng_vector_index_free(index);
    ng_graphsage_model_free(loaded_model);
    ng_graphsage_model_free(model);
    ng_close(g);
    return 0;

fail:
    ng_vector_index_free(loaded_index);
    ng_vector_index_free(index);
    ng_graphsage_model_free(loaded_model);
    ng_graphsage_model_free(model);
    ng_close(g);
    return 1;
}

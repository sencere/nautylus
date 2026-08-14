#ifndef NAUTYLUS_H
#define NAUTYLUS_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef uint64_t ng_id;
typedef ng_id ng_node_id;
typedef ng_id ng_relationship_id;
typedef ng_id ng_symbol_id;
typedef enum {
    NG_OK = 0,
    NG_INVALID_ARGUMENT,
    NG_NOT_FOUND,
    NG_PARSE_ERROR,
    NG_EXISTS,
    NG_OOM,
    NG_IO_ERROR,
    NG_CORRUPT,
    NG_LIMIT
} ng_status;
typedef enum {
    NG_VALUE_NULL = 0,
    NG_VALUE_BOOL,
    NG_VALUE_INT64,
    NG_VALUE_DOUBLE,
    NG_VALUE_STRING,
    NG_VALUE_BYTES,
    NG_VALUE_LIST,
    NG_VALUE_MAP
} ng_value_type;
typedef enum {
    NG_NODE_CONSTRAINT_REQUIRED_PROPERTY = 1,
    NG_NODE_CONSTRAINT_UNIQUE_PROPERTY = 2
} ng_node_constraint_kind;
typedef struct ng_value ng_value;
typedef struct ng_value_map_entry ng_value_map_entry;
typedef struct ng_value_map ng_value_map;
typedef struct {
    size_t count;
    ng_value* items;
} ng_value_list;
struct ng_value {
    ng_value_type type;
    size_t length;
    union {
        int boolean;
        int64_t integer;
        double real;
        const char* string;
        const unsigned char* bytes;
        const ng_value_list* list;
        const ng_value_map* map;
    } as;
};
struct ng_value_map_entry {
    const char* key;
    ng_value value;
};
struct ng_value_map {
    size_t count;
    ng_value_map_entry* entries;
};
typedef struct {
    ng_symbol_id key;
    ng_value value;
} ng_property;
typedef struct {
    const char* name;
    ng_value value;
} ng_parameter;
typedef struct ng_graph ng_graph;
typedef struct ng_transaction ng_transaction;
typedef struct ng_node_index ng_node_index;
typedef struct ng_graphsage_model ng_graphsage_model;
typedef struct ng_vector_index ng_vector_index;
typedef struct {
    uint32_t layers;
    size_t input_dimensions;
    size_t output_dimensions;
    size_t neighborhood_sample;
    int normalize_features;
    uint64_t seed;
} ng_graphsage_config;
typedef struct {
    size_t index;
    double score;
} ng_vector_score;
typedef struct {
    size_t m;
    size_t ef_construction;
    size_t ef_search;
} ng_vector_hnsw_config;
typedef enum {
    NG_GRAPHSAGE_LOSS_MSE = 0,
    NG_GRAPHSAGE_LOSS_BINARY_CROSS_ENTROPY = 1,
    NG_GRAPHSAGE_LOSS_SOFTMAX_CROSS_ENTROPY = 2
} ng_graphsage_loss_kind;
typedef struct {
    uint32_t epochs;
    double learning_rate;
    size_t batch_size;
    double validation_split;
    uint64_t seed;
    ng_graphsage_loss_kind loss;
} ng_graphsage_training_options;
typedef struct {
    double training_loss;
    double validation_loss;
    size_t training_samples;
    size_t validation_samples;
    double training_accuracy;
    double validation_accuracy;
    double training_precision;
    double validation_precision;
    double training_recall;
    double validation_recall;
} ng_graphsage_training_report;
typedef struct {
    double* epoch_training_losses;
    double* epoch_validation_losses;
    size_t epoch_capacity;
    size_t epoch_count;
    size_t epochs_run;
    int converged;
    double convergence_tolerance;
    double convergence_delta;
    size_t validation_start;
    size_t validation_row_count;
    size_t* validation_rows;
    size_t validation_row_capacity;
    uint64_t validation_seed;
    size_t batch_count;
    size_t cached_forward_reuses;
    size_t sampled_neighbor_count;
    size_t gradient_buffer_bytes;
    size_t subgraph_node_references;
    size_t subgraph_edge_references;
} ng_graphsage_training_diagnostics;
typedef enum {
    NG_PROCEDURE_SCALAR = 0,
    NG_PROCEDURE_NODE = 1,
    NG_PROCEDURE_RELATIONSHIP = 2
} ng_procedure_value_kind;
typedef struct {
    const char* name;
    ng_procedure_value_kind kind;
    ng_id id;
    ng_value value;
} ng_procedure_field;
typedef struct {
    ng_procedure_value_kind kind;
    ng_id id;
    ng_value value;
} ng_procedure_argument;
typedef struct {
    ng_procedure_field* fields;
    size_t field_count;
    size_t field_capacity;
} ng_procedure_result;
typedef ng_status (*ng_procedure_handler)(const ng_graph* graph,
                                          const ng_procedure_argument* arguments,
                                          size_t argument_count,
                                          ng_procedure_result* result,
                                          void* context);
typedef struct {
    ng_node_id id;
} ng_node;
typedef struct {
    ng_relationship_id id, source, target;
    ng_symbol_id type;
} ng_relationship;
typedef enum {
    NG_DIRECTION_OUTGOING = 0,
    NG_DIRECTION_INCOMING = 1,
    NG_DIRECTION_EITHER = 2
} ng_direction;
typedef int (*ng_relationship_visitor)(const ng_relationship* relationship, void* context);
typedef int (*ng_node_visitor)(ng_node_id node, uint32_t depth, void* context);
typedef int (*ng_path_visitor)(const ng_node_id* path, size_t length, void* context);
typedef double (*ng_path_heuristic)(ng_node_id node, ng_node_id target, void* context);
typedef int (*ng_node_match_visitor)(ng_node_id node, void* context);
typedef struct {
    ng_direction direction;
    const ng_symbol_id* types;
    size_t type_count;
    uint32_t max_depth;
    uint64_t visit_limit;
} ng_traversal_options;
typedef struct {
    size_t line;
    size_t column;
    ng_status status;
} ng_import_diagnostic;
typedef struct {
    ng_node_id node;
    double score;
} ng_node_score;
typedef struct {
    ng_node_id node;
    uint64_t value;
} ng_node_metric;
typedef struct {
    ng_node_id node;
    uint64_t component;
} ng_node_component;
typedef struct {
    ng_node_id source, target;
    double score;
} ng_link_score;
typedef struct {
    ng_direction direction;
    ng_symbol_id type;
    uint32_t max_steps;
    uint64_t seed;
} ng_random_walk_options;

ng_status ng_open(ng_graph** out, const char* path);
ng_status ng_create(ng_graph** out, const char* path);
void ng_close(ng_graph* g);
ng_status ng_save(ng_graph* g);
ng_status ng_validate(const ng_graph* g);
ng_status ng_symbol(ng_graph* g, const char* text, ng_symbol_id* out);
ng_status ng_node_create(ng_graph* g, const ng_symbol_id* labels, size_t n, ng_node_id* out);
ng_status ng_node_create_with_properties(ng_graph* g,
                                         const ng_symbol_id* labels,
                                         size_t label_count,
                                         const ng_property* properties,
                                         size_t property_count,
                                         ng_node_id* out);
ng_status ng_relationship_create(
    ng_graph* g, ng_node_id source, ng_symbol_id type, ng_node_id target, ng_relationship_id* out);
ng_status ng_relationship_delete(ng_graph* g, ng_relationship_id relationship);
ng_status ng_node_delete(ng_graph* g, ng_node_id node);
ng_status ng_node_set(ng_graph* g, ng_node_id node, ng_symbol_id key, const ng_value* v);
ng_status
ng_relationship_set(ng_graph* g, ng_relationship_id rel, ng_symbol_id key, const ng_value* v);
ng_status ng_node_unset(ng_graph* g, ng_node_id node, ng_symbol_id key);
ng_status ng_relationship_unset(ng_graph* g, ng_relationship_id rel, ng_symbol_id key);
size_t ng_node_count(const ng_graph* g);
size_t ng_relationship_count(const ng_graph* g);
size_t ng_symbol_count(const ng_graph* g);
const char* ng_symbol_name(const ng_graph* g, ng_symbol_id id);
ng_status ng_import_triples(ng_graph* g, const char* file, int preserve_parallel, size_t* accepted);
ng_status
ng_import_triples_csv(ng_graph* g, const char* file, int preserve_parallel, size_t* accepted);
ng_status ng_import_triples_diagnostic(ng_graph* g,
                                       const char* file,
                                       int preserve_parallel,
                                       size_t* accepted,
                                       ng_import_diagnostic* diagnostic);
ng_status ng_import_property_graph(ng_graph* g,
                                   const char* nodes_file,
                                   const char* relationships_file,
                                   int preserve_parallel,
                                   size_t* accepted,
                                   ng_import_diagnostic* diagnostic);
ng_status
ng_export_property_graph(const ng_graph* g, const char* nodes_file, const char* relationships_file);
ng_status ng_export_triples(const ng_graph* g, const char* file);
ng_status ng_node_get(const ng_graph* g, ng_node_id id, ng_node* out);
ng_status ng_relationship_get(const ng_graph* g, ng_relationship_id id, ng_relationship* out);
ng_status ng_node_relationships(const ng_graph* g,
                                ng_node_id node,
                                ng_direction direction,
                                ng_symbol_id type,
                                ng_relationship_visitor visitor,
                                void* context);
ng_status ng_node_has_label(const ng_graph* g, ng_node_id node, ng_symbol_id label, int* out_has);
ng_status ng_node_property(const ng_graph* g, ng_node_id node, ng_symbol_id key, ng_value* out);
ng_status ng_relationship_property(const ng_graph* g,
                                   ng_relationship_id relationship,
                                   ng_symbol_id key,
                                   ng_value* out);
ng_status ng_traverse(const ng_graph* g,
                      ng_node_id start,
                      const ng_traversal_options* options,
                      ng_node_visitor visitor,
                      void* context);
ng_status ng_degree_centrality(const ng_graph* g,
                               ng_direction direction,
                               ng_symbol_id type,
                               ng_node_score* out,
                               size_t capacity,
                               size_t* out_count);
ng_status ng_dijkstra(const ng_graph* g,
                      ng_node_id start,
                      ng_node_id target,
                      ng_direction direction,
                      ng_symbol_id type,
                      ng_symbol_id weight_key,
                      ng_node_id* out_path,
                      size_t capacity,
                      size_t* out_count,
                      double* out_distance);
ng_status ng_bfs_path(const ng_graph* g,
                      ng_node_id start,
                      ng_node_id target,
                      ng_direction direction,
                      ng_symbol_id type,
                      ng_node_id* out_path,
                      size_t capacity,
                      size_t* out_count);
ng_status ng_enumerate_paths(const ng_graph* g,
                             ng_node_id start,
                             ng_node_id target,
                             ng_direction direction,
                             ng_symbol_id type,
                             uint32_t max_depth,
                             size_t max_paths,
                             ng_path_visitor visitor,
                             void* context,
                             size_t* out_count);
ng_status ng_a_star(const ng_graph* g,
                    ng_node_id start,
                    ng_node_id target,
                    ng_direction direction,
                    ng_symbol_id type,
                    ng_symbol_id weight_key,
                    ng_path_heuristic heuristic,
                    void* heuristic_context,
                    ng_node_id* out_path,
                    size_t capacity,
                    size_t* out_count,
                    double* out_distance);
ng_status ng_pagerank(const ng_graph* g,
                      ng_symbol_id type,
                      double damping,
                      uint32_t iterations,
                      ng_node_score* out,
                      size_t capacity,
                      size_t* out_count);
ng_status ng_eigenvector_centrality(const ng_graph* g,
                                    ng_direction direction,
                                    ng_symbol_id type,
                                    uint32_t iterations,
                                    ng_node_score* out,
                                    size_t capacity,
                                    size_t* out_count);
ng_status ng_closeness_centrality(const ng_graph* g,
                                  ng_direction direction,
                                  ng_symbol_id type,
                                  ng_node_score* out,
                                  size_t capacity,
                                  size_t* out_count);
ng_status ng_harmonic_centrality(const ng_graph* g,
                                 ng_direction direction,
                                 ng_symbol_id type,
                                 ng_node_score* out,
                                 size_t capacity,
                                 size_t* out_count);
ng_status ng_fastrp(const ng_graph* g,
                    ng_direction direction,
                    ng_symbol_id type,
                    uint32_t iterations,
                    size_t dimensions,
                    uint64_t seed,
                    double* out,
                    size_t capacity,
                    size_t* out_count);
/* Deterministic lightweight Node2Vec-style random-walk embeddings. */
ng_status ng_node2vec(const ng_graph* g,
                      ng_direction direction,
                      ng_symbol_id type,
                      double p,
                      double q,
                      uint32_t walks_per_node,
                      uint32_t walk_length,
                      size_t dimensions,
                      uint64_t seed,
                      double* out,
                      size_t capacity,
                      size_t* out_count);
/* Deterministic GraphSAGE-style mean aggregation over caller-owned features. */
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
ng_status ng_graphsage_model_create(const ng_graphsage_config* config,
                                    ng_graphsage_model** out);
void ng_graphsage_model_free(ng_graphsage_model* model);
ng_status ng_graphsage_model_infer(const ng_graphsage_model* model,
                                   const ng_graph* g,
                                   ng_direction direction,
                                   ng_symbol_id type,
                                   const double* features,
                                   double* out,
                                   size_t capacity,
                                   size_t* out_count);
ng_status ng_graphsage_model_train(ng_graphsage_model* model,
                                   const ng_graph* g,
                                   ng_direction direction,
                                   ng_symbol_id type,
                                   const double* features,
                                   const double* targets,
                                   uint32_t epochs,
                                   double learning_rate,
                                   double* out_loss);
ng_status ng_graphsage_model_train_ex(ng_graphsage_model* model,
                                      const ng_graph* g,
                                      ng_direction direction,
                                      ng_symbol_id type,
                                      const double* features,
                                      const double* targets,
                                      const ng_graphsage_training_options* options,
                                      ng_graphsage_training_report* report);
ng_status ng_graphsage_model_train_ex_diagnostics(
    ng_graphsage_model* model,
    const ng_graph* g,
    ng_direction direction,
    ng_symbol_id type,
    const double* features,
    const double* targets,
    const ng_graphsage_training_options* options,
    ng_graphsage_training_report* report,
    ng_graphsage_training_diagnostics* diagnostics);
ng_status ng_graphsage_model_predict_probabilities(const ng_graphsage_model* model,
                                                   const ng_graph* g,
                                                   ng_direction direction,
                                                   ng_symbol_id type,
                                                   const double* features,
                                                   double* probabilities,
                                                   size_t capacity,
                                                   size_t* out_count);
ng_status ng_graphsage_model_predict_classes(const ng_graphsage_model* model,
                                             const ng_graph* g,
                                             ng_direction direction,
                                             ng_symbol_id type,
                                             const double* features,
                                             size_t* classes,
                                             double* confidence,
                                             size_t capacity,
                                             size_t* out_count);
ng_status ng_graphsage_model_save(const ng_graphsage_model* model, const char* path);
ng_status ng_graphsage_model_load(const char* path, ng_graphsage_model** out);
ng_status ng_vector_search_cosine(const double* vectors,
                                  size_t vector_count,
                                  size_t dimensions,
                                  const double* query,
                                  size_t k,
                                  ng_vector_score* out,
                                  size_t capacity,
                                  size_t* out_count);
ng_status ng_vector_index_create(const double* vectors,
                                 size_t vector_count,
                                 size_t dimensions,
                                 ng_vector_index** out);
ng_status ng_vector_index_create_hnsw(const double* vectors,
                                      size_t vector_count,
                                      size_t dimensions,
                                      const ng_vector_hnsw_config* config,
                                      ng_vector_index** out);
void ng_vector_index_free(ng_vector_index* index);
ng_status ng_vector_index_search_cosine(const ng_vector_index* index,
                                        const double* query,
                                        size_t k,
                                        ng_vector_score* out,
                                        size_t capacity,
                                        size_t* out_count);
ng_status ng_vector_index_search_approx_cosine(const ng_vector_index* index,
                                               const double* query,
                                               size_t k,
                                               size_t candidate_count,
                                               ng_vector_score* out,
                                               size_t capacity,
                                               size_t* out_count);
ng_status ng_vector_index_search_ann_cosine(const ng_vector_index* index,
                                            const double* query,
                                            size_t k,
                                            size_t entry_count,
                                            size_t search_budget,
                                            ng_vector_score* out,
                                            size_t capacity,
                                            size_t* out_count);
ng_status ng_vector_index_search_hnsw_cosine(const ng_vector_index* index,
                                             const double* query,
                                             size_t k,
                                             size_t ef_search,
                                             ng_vector_score* out,
                                             size_t capacity,
                                             size_t* out_count);
ng_status ng_vector_index_save(const ng_vector_index* index, const char* path);
ng_status ng_vector_index_load(const char* path, ng_vector_index** out);
ng_status ng_weakly_connected_components(const ng_graph* g,
                                         ng_symbol_id type,
                                         ng_node_component* out,
                                         size_t capacity,
                                         size_t* out_count);
ng_status ng_strongly_connected_components(const ng_graph* g,
                                           ng_symbol_id type,
                                           ng_node_component* out,
                                           size_t capacity,
                                           size_t* out_count);
ng_status ng_label_propagation(const ng_graph* g,
                               ng_direction direction,
                               ng_symbol_id type,
                               uint32_t iterations,
                               ng_node_component* out,
                               size_t capacity,
                               size_t* out_count);
ng_status ng_louvain(const ng_graph* g,
                     ng_symbol_id type,
                     uint32_t iterations,
                     ng_node_component* out,
                     size_t capacity,
                     size_t* out_count);
ng_status ng_triangle_count(
    const ng_graph* g, ng_symbol_id type, ng_node_metric* out, size_t capacity, size_t* out_count);
ng_status ng_local_clustering_coefficient(
    const ng_graph* g, ng_symbol_id type, ng_node_score* out, size_t capacity, size_t* out_count);
ng_status ng_common_neighbors(
    const ng_graph* g, ng_node_id a, ng_node_id b, ng_symbol_id type, uint64_t* out);
ng_status ng_preferential_attachment(
    const ng_graph* g, ng_node_id a, ng_node_id b, ng_symbol_id type, uint64_t* out);
ng_status
ng_total_neighbors(const ng_graph* g, ng_node_id a, ng_node_id b, ng_symbol_id type, uint64_t* out);
ng_status ng_adamic_adar(const ng_graph* g,
                         ng_node_id a,
                         ng_node_id b,
                         ng_symbol_id type,
                         double* out);
ng_status ng_resource_allocation(const ng_graph* g,
                                 ng_node_id a,
                                 ng_node_id b,
                                 ng_symbol_id type,
                                 double* out);
ng_status ng_articulation_points(const ng_graph* g,
                                 ng_symbol_id type,
                                 ng_node_id* out,
                                 size_t capacity,
                                 size_t* out_count);
ng_status ng_bridges(const ng_graph* g,
                     ng_symbol_id type,
                     ng_relationship_id* out,
                     size_t capacity,
                     size_t* out_count);
ng_status ng_minimum_spanning_tree(const ng_graph* g,
                                   ng_symbol_id type,
                                   ng_symbol_id weight_key,
                                   ng_relationship_id* out,
                                   size_t capacity,
                                   size_t* out_count,
                                   double* out_weight);
ng_status ng_max_flow(const ng_graph* g,
                      ng_node_id source,
                      ng_node_id target,
                      ng_symbol_id type,
                      ng_symbol_id capacity_key,
                      double* out_flow);
ng_status ng_knn(const ng_graph* g,
                 ng_node_id source,
                 ng_direction direction,
                 ng_symbol_id type,
                 size_t k,
                 ng_link_score* out,
                 size_t capacity,
                 size_t* out_count);
ng_status ng_knn_filtered(const ng_graph* g,
                          ng_node_id source,
                          ng_direction direction,
                          ng_symbol_id type,
                          ng_symbol_id candidate_label,
                          size_t k,
                          ng_link_score* out,
                          size_t capacity,
                          size_t* out_count);
ng_status ng_topological_sort(
    const ng_graph* g, ng_symbol_id type, ng_node_id* out, size_t capacity, size_t* out_count);
ng_status ng_random_walk(const ng_graph* g,
                         ng_node_id start,
                         const ng_random_walk_options* options,
                         ng_node_id* out,
                         size_t capacity,
                         size_t* out_count);
ng_status ng_find_nodes(const ng_graph* g,
                        ng_symbol_id label,
                        ng_symbol_id key,
                        const ng_value* value,
                        ng_node_match_visitor visitor,
                        void* context);
ng_status ng_require_node_property(const ng_graph* g,
                                   ng_symbol_id label,
                                   ng_symbol_id key,
                                   ng_node_id* out_node);
ng_status ng_unique_node_property(const ng_graph* g,
                                  ng_symbol_id label,
                                  ng_symbol_id key,
                                  ng_node_id* out_first,
                                  ng_node_id* out_second);
ng_status ng_node_constraint_create(ng_graph* g,
                                    ng_node_constraint_kind kind,
                                    ng_symbol_id label,
                                    ng_symbol_id key);
ng_status ng_node_constraint_drop(ng_graph* g,
                                  ng_node_constraint_kind kind,
                                  ng_symbol_id label,
                                  ng_symbol_id key);
size_t ng_node_constraint_count(const ng_graph* g);
ng_status ng_node_constraint_get(const ng_graph* g,
                                 size_t index,
                                 ng_node_constraint_kind* kind,
                                 ng_symbol_id* label,
                                 ng_symbol_id* key);
ng_status ng_node_index_create(ng_graph* g, ng_symbol_id label, ng_symbol_id key);
ng_status ng_node_index_drop(ng_graph* g, ng_symbol_id label, ng_symbol_id key);
size_t ng_node_index_count(const ng_graph* g);
ng_status
ng_node_index_get(const ng_graph* g, size_t index, ng_symbol_id* label, ng_symbol_id* key);
ng_status
ng_procedure_register(ng_graph* g, const char* name, ng_procedure_handler handler, void* context);
ng_status ng_procedure_unregister(ng_graph* g, const char* name);
ng_status ng_transaction_begin(ng_graph* g, ng_transaction** out);
ng_graph* ng_transaction_graph(ng_transaction* transaction);
ng_status ng_transaction_commit(ng_transaction* transaction);
void ng_transaction_rollback(ng_transaction* transaction);
ng_status
ng_node_index_build(const ng_graph* g, ng_symbol_id label, ng_symbol_id key, ng_node_index** out);
ng_status ng_node_index_find(const ng_node_index* index,
                             const ng_value* value,
                             ng_node_match_visitor visitor,
                             void* context);
void ng_node_index_free(ng_node_index* index);
ng_status
ng_query_nodes(const ng_graph* g, const char* query, ng_node_match_visitor visitor, void* context);
ng_status ng_query_explain(const char* query, char* buffer, size_t capacity);
ng_status ng_query_print(const ng_graph* g, const char* query, FILE* out);
ng_status ng_query_print_params(const ng_graph* g,
                                const char* query,
                                const ng_parameter* parameters,
                                size_t parameter_count,
                                FILE* out);
ng_status ng_query_execute(ng_graph* g, const char* query, FILE* out, int* mutated);
ng_status ng_query_execute_params(ng_graph* g,
                                  const char* query,
                                  const ng_parameter* parameters,
                                  size_t parameter_count,
                                  FILE* out,
                                  int* mutated);
const char* ng_status_name(ng_status s);
#endif

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
    NG_VALUE_LIST
} ng_value_type;
typedef enum {
    NG_NODE_CONSTRAINT_REQUIRED_PROPERTY = 1,
    NG_NODE_CONSTRAINT_UNIQUE_PROPERTY = 2
} ng_node_constraint_kind;
typedef struct ng_value ng_value;
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
    } as;
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
ng_status ng_pagerank(const ng_graph* g,
                      ng_symbol_id type,
                      double damping,
                      uint32_t iterations,
                      ng_node_score* out,
                      size_t capacity,
                      size_t* out_count);
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

#ifndef NAUTYLUS_GRAPH_H
#define NAUTYLUS_GRAPH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "nautylus/ids.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nty_graph nty_graph_t;

typedef struct nty_node {
  nty_id_t id;
  nty_id_t out_head;
  nty_id_t out_tail;
  nty_id_t in_head;
  nty_id_t in_tail;
} nty_node_t;

typedef struct nty_edge {
  nty_id_t id;
  nty_id_t from;
  nty_id_t to;
  uint64_t type;
  double weight;
  uint64_t timestamp;
  nty_id_t out_prev;
  nty_id_t out_next;
  nty_id_t in_prev;
  nty_id_t in_next;
} nty_edge_t;

typedef struct nty_property {
  nty_id_t id;
  nty_id_t owner;
  uint64_t key;
  double value;
} nty_property_t;

/*
 * Deterministic iteration order:
 * - Nodes/edges/properties iterate in creation order.
 * - Deletions preserve the relative order of remaining items.
 * Given the same mutation sequence, iteration is stable across runs.
 */

nty_graph_t *nty_graph_create(size_t node_capacity,
                              size_t edge_capacity,
                              size_t property_capacity);
void nty_graph_destroy(nty_graph_t *graph);

nty_id_t nty_node_create(nty_graph_t *graph);
bool nty_node_destroy(nty_graph_t *graph, nty_id_t node_id);

nty_id_t nty_edge_create(nty_graph_t *graph,
                         nty_id_t from,
                         nty_id_t to,
                         uint64_t type,
                         double weight,
                         uint64_t timestamp);
bool nty_edge_destroy(nty_graph_t *graph, nty_id_t edge_id);

nty_id_t nty_property_create(nty_graph_t *graph,
                             nty_id_t owner,
                             uint64_t key,
                             double value);
bool nty_property_destroy(nty_graph_t *graph, nty_id_t prop_id);

bool nty_node_exists(const nty_graph_t *graph, nty_id_t node_id);
bool nty_edge_exists(const nty_graph_t *graph, nty_id_t edge_id);
bool nty_property_exists(const nty_graph_t *graph, nty_id_t prop_id);

bool nty_node_get(const nty_graph_t *graph, nty_id_t node_id, nty_node_t *out);
bool nty_edge_get(const nty_graph_t *graph, nty_id_t edge_id, nty_edge_t *out);
bool nty_property_get(const nty_graph_t *graph,
                      nty_id_t prop_id,
                      nty_property_t *out);

size_t nty_graph_node_count(const nty_graph_t *graph);
size_t nty_graph_edge_count(const nty_graph_t *graph);
size_t nty_graph_property_count(const nty_graph_t *graph);

typedef struct nty_node_iter {
  size_t index;
} nty_node_iter_t;

typedef struct nty_edge_iter {
  size_t index;
} nty_edge_iter_t;

typedef struct nty_property_iter {
  size_t index;
} nty_property_iter_t;

nty_node_iter_t nty_graph_nodes(const nty_graph_t *graph);
nty_edge_iter_t nty_graph_edges(const nty_graph_t *graph);
nty_property_iter_t nty_graph_properties(const nty_graph_t *graph);

bool nty_node_iter_next(const nty_graph_t *graph,
                        nty_node_iter_t *iter,
                        nty_id_t *out_id);
bool nty_edge_iter_next(const nty_graph_t *graph,
                        nty_edge_iter_t *iter,
                        nty_id_t *out_id);
bool nty_property_iter_next(const nty_graph_t *graph,
                            nty_property_iter_t *iter,
                            nty_id_t *out_id);

typedef struct nty_neighbor_iter {
  nty_id_t current;
  bool outgoing;
} nty_neighbor_iter_t;

nty_neighbor_iter_t nty_node_out_edges(const nty_graph_t *graph, nty_id_t node);
nty_neighbor_iter_t nty_node_in_edges(const nty_graph_t *graph, nty_id_t node);
bool nty_neighbor_iter_next(const nty_graph_t *graph,
                            nty_neighbor_iter_t *iter,
                            nty_id_t *out_edge);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // NAUTYLUS_GRAPH_H

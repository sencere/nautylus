#include "nautylus/graph.h"

#include <stdlib.h>
#include <string.h>

#include "nautylus_slab.h"

struct nty_graph {
  nty_slab_t nodes;
  nty_slab_t edges;
  nty_slab_t properties;
  nty_id_t *node_order;
  nty_id_t *edge_order;
  nty_id_t *property_order;
  size_t node_order_len;
  size_t edge_order_len;
  size_t property_order_len;
  size_t node_order_cap;
  size_t edge_order_cap;
  size_t property_order_cap;
};

static nty_node_t *nty_graph_get_node(nty_graph_t *graph, nty_id_t node_id) {
  return (nty_node_t *)nty_slab_get(&graph->nodes, node_id);
}

static const nty_node_t *nty_graph_get_node_const(const nty_graph_t *graph,
                                                  nty_id_t node_id) {
  return (const nty_node_t *)nty_slab_get_const(&graph->nodes, node_id);
}

static nty_edge_t *nty_graph_get_edge(nty_graph_t *graph, nty_id_t edge_id) {
  return (nty_edge_t *)nty_slab_get(&graph->edges, edge_id);
}

static bool nty_order_append(nty_id_t *order,
                             size_t cap,
                             size_t *len,
                             nty_id_t id) {
  if (!order || !len || *len >= cap) {
    return false;
  }
  order[*len] = id;
  (*len)++;
  return true;
}

static void nty_order_remove(nty_id_t *order, size_t *len, nty_id_t id) {
  if (!order || !len || *len == 0) {
    return;
  }
  for (size_t i = 0; i < *len; i++) {
    if (order[i] == id) {
      size_t remaining = *len - i - 1;
      if (remaining > 0) {
        memmove(&order[i], &order[i + 1], remaining * sizeof(nty_id_t));
      }
      (*len)--;
      return;
    }
  }
}

nty_graph_t *nty_graph_create(size_t node_capacity,
                              size_t edge_capacity,
                              size_t property_capacity) {
  nty_graph_t *graph = (nty_graph_t *)calloc(1, sizeof(nty_graph_t));
  if (!graph) {
    return NULL;
  }

  if (!nty_slab_init(&graph->nodes, sizeof(nty_node_t), (uint32_t)node_capacity) ||
      !nty_slab_init(&graph->edges, sizeof(nty_edge_t), (uint32_t)edge_capacity) ||
      !nty_slab_init(&graph->properties, sizeof(nty_property_t),
                     (uint32_t)property_capacity)) {
    nty_graph_destroy(graph);
    return NULL;
  }

  graph->node_order_cap = node_capacity;
  graph->edge_order_cap = edge_capacity;
  graph->property_order_cap = property_capacity;
  graph->node_order = (nty_id_t *)calloc(node_capacity, sizeof(nty_id_t));
  graph->edge_order = (nty_id_t *)calloc(edge_capacity, sizeof(nty_id_t));
  graph->property_order =
      (nty_id_t *)calloc(property_capacity, sizeof(nty_id_t));
  if ((!graph->node_order && node_capacity > 0) ||
      (!graph->edge_order && edge_capacity > 0) ||
      (!graph->property_order && property_capacity > 0)) {
    nty_graph_destroy(graph);
    return NULL;
  }

  return graph;
}

static void nty_edge_unlink(nty_graph_t *graph, nty_edge_t *edge) {
  if (!graph || !edge) {
    return;
  }

  nty_node_t *from = nty_graph_get_node(graph, edge->from);
  if (from) {
    if (edge->out_prev) {
      nty_edge_t *prev = nty_graph_get_edge(graph, edge->out_prev);
      if (prev) {
        prev->out_next = edge->out_next;
      }
    } else {
      from->out_head = edge->out_next;
    }
    if (edge->out_next) {
      nty_edge_t *next = nty_graph_get_edge(graph, edge->out_next);
      if (next) {
        next->out_prev = edge->out_prev;
      }
    } else {
      from->out_tail = edge->out_prev;
    }
  }

  nty_node_t *to = nty_graph_get_node(graph, edge->to);
  if (to) {
    if (edge->in_prev) {
      nty_edge_t *prev = nty_graph_get_edge(graph, edge->in_prev);
      if (prev) {
        prev->in_next = edge->in_next;
      }
    } else {
      to->in_head = edge->in_next;
    }
    if (edge->in_next) {
      nty_edge_t *next = nty_graph_get_edge(graph, edge->in_next);
      if (next) {
        next->in_prev = edge->in_prev;
      }
    } else {
      to->in_tail = edge->in_prev;
    }
  }
}

void nty_graph_destroy(nty_graph_t *graph) {
  if (!graph) {
    return;
  }
  nty_slab_destroy(&graph->nodes);
  nty_slab_destroy(&graph->edges);
  nty_slab_destroy(&graph->properties);
  free(graph->node_order);
  free(graph->edge_order);
  free(graph->property_order);
  free(graph);
}

nty_id_t nty_node_create(nty_graph_t *graph) {
  if (!graph) {
    return 0;
  }
  nty_node_t *node = NULL;
  nty_id_t id = nty_slab_alloc(&graph->nodes, (void **)&node);
  if (id == 0) {
    return 0;
  }
  node->id = id;
  if (!nty_order_append(graph->node_order, graph->node_order_cap,
                        &graph->node_order_len, id)) {
    nty_slab_free(&graph->nodes, id);
    return 0;
  }
  return id;
}

bool nty_node_destroy(nty_graph_t *graph, nty_id_t node_id) {
  if (!graph) {
    return false;
  }
  nty_node_t *node = nty_graph_get_node(graph, node_id);
  if (!node) {
    return false;
  }
  while (node->out_head) {
    nty_edge_destroy(graph, node->out_head);
  }
  while (node->in_head) {
    nty_edge_destroy(graph, node->in_head);
  }
  if (!nty_slab_free(&graph->nodes, node_id)) {
    return false;
  }
  nty_order_remove(graph->node_order, &graph->node_order_len, node_id);
  return true;
}

nty_id_t nty_edge_create(nty_graph_t *graph,
                         nty_id_t from,
                         nty_id_t to,
                         uint64_t type,
                         double weight,
                         uint64_t timestamp) {
  if (!graph) {
    return 0;
  }
  nty_node_t *from_node = nty_graph_get_node(graph, from);
  nty_node_t *to_node = nty_graph_get_node(graph, to);
  if (!from_node || !to_node) {
    return 0;
  }
  nty_edge_t *edge = NULL;
  nty_id_t id = nty_slab_alloc(&graph->edges, (void **)&edge);
  if (id == 0) {
    return 0;
  }
  edge->id = id;
  edge->from = from;
  edge->to = to;
  edge->type = type;
  edge->weight = weight;
  edge->timestamp = timestamp;
  edge->out_prev = from_node->out_tail;
  edge->out_next = 0;
  edge->in_prev = to_node->in_tail;
  edge->in_next = 0;
  if (from_node->out_tail) {
    nty_edge_t *prev = nty_graph_get_edge(graph, from_node->out_tail);
    if (prev) {
      prev->out_next = id;
    }
  } else {
    from_node->out_head = id;
  }
  from_node->out_tail = id;
  if (to_node->in_tail) {
    nty_edge_t *prev = nty_graph_get_edge(graph, to_node->in_tail);
    if (prev) {
      prev->in_next = id;
    }
  } else {
    to_node->in_head = id;
  }
  to_node->in_tail = id;
  if (!nty_order_append(graph->edge_order, graph->edge_order_cap,
                        &graph->edge_order_len, id)) {
    nty_edge_unlink(graph, edge);
    nty_slab_free(&graph->edges, id);
    return 0;
  }
  return id;
}

bool nty_edge_destroy(nty_graph_t *graph, nty_id_t edge_id) {
  if (!graph) {
    return false;
  }
  nty_edge_t *edge = nty_graph_get_edge(graph, edge_id);
  if (!edge) {
    return false;
  }
  nty_edge_unlink(graph, edge);
  if (!nty_slab_free(&graph->edges, edge_id)) {
    return false;
  }
  nty_order_remove(graph->edge_order, &graph->edge_order_len, edge_id);
  return true;
}

nty_id_t nty_property_create(nty_graph_t *graph,
                             nty_id_t owner,
                             uint64_t key,
                             double value) {
  if (!graph) {
    return 0;
  }
  nty_property_t *prop = NULL;
  nty_id_t id = nty_slab_alloc(&graph->properties, (void **)&prop);
  if (id == 0) {
    return 0;
  }
  prop->id = id;
  prop->owner = owner;
  prop->key = key;
  prop->value = value;
  if (!nty_order_append(graph->property_order, graph->property_order_cap,
                        &graph->property_order_len, id)) {
    nty_slab_free(&graph->properties, id);
    return 0;
  }
  return id;
}

bool nty_property_destroy(nty_graph_t *graph, nty_id_t prop_id) {
  if (!graph) {
    return false;
  }
  if (!nty_slab_free(&graph->properties, prop_id)) {
    return false;
  }
  nty_order_remove(graph->property_order, &graph->property_order_len, prop_id);
  return true;
}

bool nty_node_exists(const nty_graph_t *graph, nty_id_t node_id) {
  if (!graph) {
    return false;
  }
  return nty_slab_exists(&graph->nodes, node_id);
}

bool nty_edge_exists(const nty_graph_t *graph, nty_id_t edge_id) {
  if (!graph) {
    return false;
  }
  return nty_slab_exists(&graph->edges, edge_id);
}

bool nty_property_exists(const nty_graph_t *graph, nty_id_t prop_id) {
  if (!graph) {
    return false;
  }
  return nty_slab_exists(&graph->properties, prop_id);
}

bool nty_node_get(const nty_graph_t *graph, nty_id_t node_id, nty_node_t *out) {
  if (!graph || !out) {
    return false;
  }
  const nty_node_t *node =
      (const nty_node_t *)nty_slab_get_const(&graph->nodes, node_id);
  if (!node) {
    return false;
  }
  *out = *node;
  return true;
}

bool nty_edge_get(const nty_graph_t *graph, nty_id_t edge_id, nty_edge_t *out) {
  if (!graph || !out) {
    return false;
  }
  const nty_edge_t *edge =
      (const nty_edge_t *)nty_slab_get_const(&graph->edges, edge_id);
  if (!edge) {
    return false;
  }
  *out = *edge;
  return true;
}

bool nty_property_get(const nty_graph_t *graph,
                      nty_id_t prop_id,
                      nty_property_t *out) {
  if (!graph || !out) {
    return false;
  }
  const nty_property_t *prop =
      (const nty_property_t *)nty_slab_get_const(&graph->properties, prop_id);
  if (!prop) {
    return false;
  }
  *out = *prop;
  return true;
}

size_t nty_graph_node_count(const nty_graph_t *graph) {
  return graph ? graph->nodes.count : 0;
}

size_t nty_graph_edge_count(const nty_graph_t *graph) {
  return graph ? graph->edges.count : 0;
}

size_t nty_graph_property_count(const nty_graph_t *graph) {
  return graph ? graph->properties.count : 0;
}

nty_node_iter_t nty_graph_nodes(const nty_graph_t *graph) {
  nty_node_iter_t iter;
  iter.index = 0;
  (void)graph;
  return iter;
}

nty_edge_iter_t nty_graph_edges(const nty_graph_t *graph) {
  nty_edge_iter_t iter;
  iter.index = 0;
  (void)graph;
  return iter;
}

nty_property_iter_t nty_graph_properties(const nty_graph_t *graph) {
  nty_property_iter_t iter;
  iter.index = 0;
  (void)graph;
  return iter;
}

bool nty_node_iter_next(const nty_graph_t *graph,
                        nty_node_iter_t *iter,
                        nty_id_t *out_id) {
  if (!graph || !iter || !out_id) {
    return false;
  }
  while (iter->index < graph->node_order_len) {
    nty_id_t id = graph->node_order[iter->index++];
    if (nty_slab_exists(&graph->nodes, id)) {
      *out_id = id;
      return true;
    }
  }
  return false;
}

bool nty_edge_iter_next(const nty_graph_t *graph,
                        nty_edge_iter_t *iter,
                        nty_id_t *out_id) {
  if (!graph || !iter || !out_id) {
    return false;
  }
  while (iter->index < graph->edge_order_len) {
    nty_id_t id = graph->edge_order[iter->index++];
    if (nty_slab_exists(&graph->edges, id)) {
      *out_id = id;
      return true;
    }
  }
  return false;
}

bool nty_property_iter_next(const nty_graph_t *graph,
                            nty_property_iter_t *iter,
                            nty_id_t *out_id) {
  if (!graph || !iter || !out_id) {
    return false;
  }
  while (iter->index < graph->property_order_len) {
    nty_id_t id = graph->property_order[iter->index++];
    if (nty_slab_exists(&graph->properties, id)) {
      *out_id = id;
      return true;
    }
  }
  return false;
}

nty_neighbor_iter_t nty_node_out_edges(const nty_graph_t *graph,
                                       nty_id_t node) {
  nty_neighbor_iter_t iter;
  iter.outgoing = true;
  if (!graph) {
    iter.current = 0;
    return iter;
  }
  const nty_node_t *node_ptr = nty_graph_get_node_const(graph, node);
  iter.current = node_ptr ? node_ptr->out_head : 0;
  return iter;
}

nty_neighbor_iter_t nty_node_in_edges(const nty_graph_t *graph, nty_id_t node) {
  nty_neighbor_iter_t iter;
  iter.outgoing = false;
  if (!graph) {
    iter.current = 0;
    return iter;
  }
  const nty_node_t *node_ptr = nty_graph_get_node_const(graph, node);
  iter.current = node_ptr ? node_ptr->in_head : 0;
  return iter;
}

bool nty_neighbor_iter_next(const nty_graph_t *graph,
                            nty_neighbor_iter_t *iter,
                            nty_id_t *out_edge) {
  if (!graph || !iter || !out_edge) {
    return false;
  }
  if (iter->current == 0) {
    return false;
  }
  nty_edge_t *edge = nty_graph_get_edge((nty_graph_t *)graph, iter->current);
  if (!edge) {
    return false;
  }
  *out_edge = iter->current;
  iter->current = iter->outgoing ? edge->out_next : edge->in_next;
  return true;
}

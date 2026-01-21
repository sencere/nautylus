#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "nautylus/graph.h"

#define ASSERT_OK(cond)                                                  \
  do {                                                                   \
    if (!(cond)) {                                                       \
      fprintf(stderr, "assertion failed at %s:%d: %s\n", __FILE__,        \
              __LINE__, #cond);                                          \
      return 1;                                                          \
    }                                                                    \
  } while (0)

typedef struct id_pool {
  nty_id_t *ids;
  size_t len;
  size_t cap;
} id_pool_t;

static void pool_init(id_pool_t *pool, nty_id_t *buffer, size_t cap) {
  pool->ids = buffer;
  pool->len = 0;
  pool->cap = cap;
}

static void pool_add(id_pool_t *pool, nty_id_t id) {
  if (pool->len < pool->cap) {
    pool->ids[pool->len++] = id;
  }
}

static void pool_remove_index(id_pool_t *pool, size_t index) {
  if (pool->len == 0 || index >= pool->len) {
    return;
  }
  pool->ids[index] = pool->ids[pool->len - 1];
  pool->len--;
}

static void pool_compact_edges(nty_graph_t *graph, id_pool_t *pool) {
  size_t i = 0;
  while (i < pool->len) {
    if (!nty_edge_exists(graph, pool->ids[i])) {
      pool_remove_index(pool, i);
      continue;
    }
    i++;
  }
}

static uint64_t rng_state = 0x9E3779B97F4A7C15ull;

static uint64_t rng_next(void) {
  rng_state ^= rng_state >> 12;
  rng_state ^= rng_state << 25;
  rng_state ^= rng_state >> 27;
  return rng_state * 2685821657736338717ull;
}

static int verify_invariants(nty_graph_t *graph) {
  nty_edge_iter_t eiter = nty_graph_edges(graph);
  nty_id_t edge_id = 0;
  while (nty_edge_iter_next(graph, &eiter, &edge_id)) {
    nty_edge_t edge;
    ASSERT_OK(nty_edge_get(graph, edge_id, &edge));
    ASSERT_OK(nty_node_exists(graph, edge.from));
    ASSERT_OK(nty_node_exists(graph, edge.to));
  }

  nty_node_iter_t niter = nty_graph_nodes(graph);
  nty_id_t node_id = 0;
  while (nty_node_iter_next(graph, &niter, &node_id)) {
    nty_neighbor_iter_t out_iter = nty_node_out_edges(graph, node_id);
    while (nty_neighbor_iter_next(graph, &out_iter, &edge_id)) {
      nty_edge_t edge;
      ASSERT_OK(nty_edge_get(graph, edge_id, &edge));
      ASSERT_OK(edge.from == node_id);
    }
    nty_neighbor_iter_t in_iter = nty_node_in_edges(graph, node_id);
    while (nty_neighbor_iter_next(graph, &in_iter, &edge_id)) {
      nty_edge_t edge;
      ASSERT_OK(nty_edge_get(graph, edge_id, &edge));
      ASSERT_OK(edge.to == node_id);
    }
  }
  return 0;
}

static int test_basic(void) {
  nty_graph_t *graph = nty_graph_create(8, 8, 8);
  ASSERT_OK(graph != NULL);

  nty_id_t a = nty_node_create(graph);
  nty_id_t b = nty_node_create(graph);
  nty_id_t c = nty_node_create(graph);
  ASSERT_OK(a && b && c);

  nty_id_t e1 = nty_edge_create(graph, a, b, 1, 0.5, 10);
  nty_id_t e2 = nty_edge_create(graph, a, c, 2, 1.0, 20);
  ASSERT_OK(e1 && e2);

  nty_node_iter_t niter = nty_graph_nodes(graph);
  nty_id_t node_id = 0;
  size_t node_count = 0;
  while (nty_node_iter_next(graph, &niter, &node_id)) {
    node_count++;
  }
  ASSERT_OK(node_count == 3);

  nty_neighbor_iter_t out_iter = nty_node_out_edges(graph, a);
  nty_id_t out_edge = 0;
  ASSERT_OK(nty_neighbor_iter_next(graph, &out_iter, &out_edge));
  ASSERT_OK(out_edge == e1);
  ASSERT_OK(nty_neighbor_iter_next(graph, &out_iter, &out_edge));
  ASSERT_OK(out_edge == e2);
  ASSERT_OK(!nty_neighbor_iter_next(graph, &out_iter, &out_edge));

  nty_graph_destroy(graph);
  return 0;
}

static int test_referential_integrity(void) {
  nty_graph_t *graph = nty_graph_create(8, 8, 8);
  ASSERT_OK(graph != NULL);

  nty_id_t a = nty_node_create(graph);
  ASSERT_OK(a);

  nty_id_t invalid = nty_edge_create(graph, a, 999, 1, 0.0, 0);
  ASSERT_OK(invalid == 0);

  nty_id_t b = nty_node_create(graph);
  ASSERT_OK(b);
  nty_id_t e1 = nty_edge_create(graph, a, b, 1, 0.0, 0);
  ASSERT_OK(e1);
  ASSERT_OK(nty_graph_edge_count(graph) == 1);

  ASSERT_OK(nty_node_destroy(graph, a));
  ASSERT_OK(nty_graph_edge_count(graph) == 0);
  ASSERT_OK(!nty_edge_exists(graph, e1));

  nty_graph_destroy(graph);
  return 0;
}

static int test_random_mutations(void) {
  const size_t max_nodes = 64;
  const size_t max_edges = 256;
  nty_graph_t *graph = nty_graph_create(max_nodes, max_edges, 8);
  ASSERT_OK(graph != NULL);

  nty_id_t node_ids[64];
  nty_id_t edge_ids[256];
  id_pool_t nodes;
  id_pool_t edges;
  pool_init(&nodes, node_ids, max_nodes);
  pool_init(&edges, edge_ids, max_edges);

  for (size_t step = 0; step < 2000; step++) {
    uint64_t r = rng_next();
    int op = (int)(r % 4);

    if (op == 0 && nodes.len < nodes.cap) {
      nty_id_t id = nty_node_create(graph);
      if (id) {
        pool_add(&nodes, id);
      }
    } else if (op == 1 && edges.len < edges.cap && nodes.len >= 2) {
      size_t from_idx = (size_t)(r % nodes.len);
      size_t to_idx = (size_t)((r >> 8) % nodes.len);
      nty_id_t from = nodes.ids[from_idx];
      nty_id_t to = nodes.ids[to_idx];
      nty_id_t id = nty_edge_create(graph, from, to, 1, 0.0, step);
      if (id) {
        pool_add(&edges, id);
      }
    } else if (op == 2 && nodes.len > 0) {
      size_t idx = (size_t)(r % nodes.len);
      nty_id_t id = nodes.ids[idx];
      if (nty_node_destroy(graph, id)) {
        pool_remove_index(&nodes, idx);
        pool_compact_edges(graph, &edges);
      }
    } else if (op == 3 && edges.len > 0) {
      size_t idx = (size_t)(r % edges.len);
      nty_id_t id = edges.ids[idx];
      nty_edge_destroy(graph, id);
      pool_remove_index(&edges, idx);
    }

    ASSERT_OK(verify_invariants(graph) == 0);
  }

  nty_graph_destroy(graph);
  return 0;
}

int main(void) {
  ASSERT_OK(test_basic() == 0);
  ASSERT_OK(test_referential_integrity() == 0);
  ASSERT_OK(test_random_mutations() == 0);
  printf("graph tests passed\n");
  return 0;
}
